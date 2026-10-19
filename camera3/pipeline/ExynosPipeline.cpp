#define LOG_TAG "Smdk4x12Pipeline"

#include "ExynosPipeline.h"

#include "ExynosCameraControls.h"

#include <log/log.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace smdk4x12 {

extern int xioctl(int fd, unsigned long request, void *argument);

namespace {

// The N8000 Exynos4 reserved camera pool can hold four full-size buffers.
constexpr int kCaptureBufferCount = 4;
constexpr int kDequeueTimeoutMs = 1500;

// Native S5K6A3 FIMC-IS sensor window.
void s5k6a3MbusSize(uint32_t width, uint32_t height, uint32_t *mbusWidth,
                    uint32_t *mbusHeight) {
  if (width == 1280 && height == 720) {
    *mbusWidth = 1344;
    *mbusHeight = 756;
  } else if (width == height) {
    *mbusWidth = 1392;
    *mbusHeight = 1392;
  } else {
    *mbusWidth = 1392;
    *mbusHeight = 1044;
  }
}

int64_t monotonicNs() {
  timespec time{};
  clock_gettime(CLOCK_MONOTONIC, &time);
  return static_cast<int64_t>(time.tv_sec) * 1000000000LL + time.tv_nsec;
}

} // namespace

ExynosPipeline::ExynosPipeline() = default;

ExynosPipeline::~ExynosPipeline() { close(); }

int ExynosPipeline::open(int cameraId, bool fimcIs) {
  close();

  std::lock_guard<std::mutex> guard(lock_);
  fd_ = ::open("/dev/video0", O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0)
    return -errno;

  abortFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (abortFd_ < 0) {
    const int error = -errno;
    ::close(fd_);
    fd_ = -1;
    return error;
  }

  int input = cameraId;
  if (xioctl(fd_, VIDIOC_S_INPUT, &input) < 0) {
    const int error = -errno;
    ::close(abortFd_);
    ::close(fd_);
    abortFd_ = -1;
    fd_ = -1;
    return error;
  }

  cameraId_ = cameraId;
  fimcIs_ = fimcIs;
  return 0;
}

int ExynosPipeline::setControl(uint32_t id, int value) {
  std::lock_guard<std::mutex> guard(lock_);
  if (fd_ < 0)
    return -ENODEV;
  v4l2_control control{};
  control.id = id;
  control.value = value;
  return xioctl(fd_, VIDIOC_S_CTRL, &control) < 0 ? -errno : 0;
}

int ExynosPipeline::getControl(uint32_t id, int *value) {
  std::lock_guard<std::mutex> guard(lock_);
  if (fd_ < 0)
    return -ENODEV;
  v4l2_control control{};
  control.id = id;
  if (xioctl(fd_, VIDIOC_G_CTRL, &control) < 0)
    return -errno;
  if (value)
    *value = control.value;
  return 0;
}

bool ExynosPipeline::dmabufSupported() const {
  std::lock_guard<std::mutex> guard(lock_);
  return dmabufSupported_;
}

int ExynosPipeline::configure(uint32_t width, uint32_t height, uint32_t format,
                              int fps) {
  stop();
  std::lock_guard<std::mutex> guard(lock_);
  if (fd_ < 0 || !width || !height || fps <= 0)
    return -EINVAL;

  if (fimcIs_) {
    uint32_t mbusWidth;
    uint32_t mbusHeight;
    s5k6a3MbusSize(width, height, &mbusWidth, &mbusHeight);

    v4l2_control scenario{.id = V4L2_CID_IS_S_FORMAT_SCENARIO,
                          .value = IS_MODE_PREVIEW_STILL};
    if (xioctl(fd_, VIDIOC_S_CTRL, &scenario) < 0) {
      ALOGE("FIMC-IS format scenario failed: %s", strerror(errno));
      return -errno;
    }

    v4l2_format busFormat{};
    busFormat.type = V4L2_BUF_TYPE_PRIVATE;
    busFormat.fmt.pix.width = mbusWidth;
    busFormat.fmt.pix.height = mbusHeight;
    busFormat.fmt.pix.pixelformat = format;
    busFormat.fmt.pix.field = IS_MODE_PREVIEW_STILL;
    busFormat.fmt.pix.priv = V4L2_PIX_FMT_MODE_PREVIEW;
    if (xioctl(fd_, VIDIOC_S_FMT, &busFormat) < 0) {
      ALOGE("FIMC-IS MBUS S_FMT %ux%u for %ux%u failed: %s", mbusWidth,
            mbusHeight, width, height, strerror(errno));
      return -errno;
    }
  }

  v4l2_format captureFormat{};
  captureFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  captureFormat.fmt.pix.width = width;
  captureFormat.fmt.pix.height = height;
  captureFormat.fmt.pix.pixelformat = format;
  captureFormat.fmt.pix.field = V4L2_FIELD_NONE;
  captureFormat.fmt.pix.priv = V4L2_PIX_FMT_MODE_PREVIEW;
  if (xioctl(fd_, VIDIOC_S_FMT, &captureFormat) < 0) {
    ALOGE("capture S_FMT %ux%u fmt=%c%c%c%c failed: %s", width, height,
          format & 0xff, (format >> 8) & 0xff, (format >> 16) & 0xff,
          (format >> 24) & 0xff, strerror(errno));
    return -errno;
  }

  if (!fimcIs_) {
    v4l2_format busFormat{};
    busFormat.type = V4L2_BUF_TYPE_PRIVATE;
    busFormat.fmt.pix.width = width;
    busFormat.fmt.pix.height = height;
    busFormat.fmt.pix.pixelformat = format;
    busFormat.fmt.pix.field = V4L2_FIELD_NONE;
    busFormat.fmt.pix.priv = V4L2_PIX_FMT_MODE_PREVIEW;
    if (xioctl(fd_, VIDIOC_S_FMT, &busFormat) < 0)
      return -errno;

    v4l2_control control{.id = V4L2_CID_CACHEABLE, .value = 0};
    (void)xioctl(fd_, VIDIOC_S_CTRL, &control);
    control = {.id = V4L2_CID_EMBEDDEDDATA_ENABLE, .value = 1};
    (void)xioctl(fd_, VIDIOC_S_CTRL, &control);
    control = {.id = V4L2_CID_CAMERA_HYBRID, .value = 1};
    (void)xioctl(fd_, VIDIOC_S_CTRL, &control);
  } else {
    v4l2_control control{.id = V4L2_CID_CACHEABLE, .value = 1};
    (void)xioctl(fd_, VIDIOC_S_CTRL, &control);
    control = {.id = V4L2_CID_IS_S_SCENARIO_MODE,
               .value = IS_MODE_PREVIEW_STILL};
    (void)xioctl(fd_, VIDIOC_S_CTRL, &control);
  }

  v4l2_streamparm parameters{};
  parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parameters.parm.capture.timeperframe.numerator = 1;
  parameters.parm.capture.timeperframe.denominator = fps;
  if (xioctl(fd_, VIDIOC_S_PARM, &parameters) < 0) {
    ALOGW("VIDIOC_S_PARM failed: %s", strerror(errno));
  }

  v4l2_requestbuffers request{};
  int requestError = ENOMEM;
  bool buffersAllocated = false;
  for (int count = kCaptureBufferCount; count > 0; --count) {
    request = {};
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    request.count = count;
    if (xioctl(fd_, VIDIOC_REQBUFS, &request) == 0) {
      buffersAllocated = request.count != 0;
      break;
    }
    requestError = errno;
    ALOGW("VIDIOC_REQBUFS count=%d failed: %s", count, strerror(requestError));
  }
  if (!buffersAllocated)
    return -requestError;

  buffers_.resize(request.count);
  dmabufSupported_ = true;

  for (uint32_t index = 0; index < request.count; ++index) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
      const int error = -errno;
      releaseBuffersLocked();
      return error;
    }

    void *pointer = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd_, buffer.m.offset);
    if (pointer == MAP_FAILED) {
      const int error = -errno;
      releaseBuffersLocked();
      return error;
    }

    buffers_[index].ptr = pointer;
    buffers_[index].length = buffer.length;

#ifdef VIDIOC_EXPBUF
    v4l2_exportbuffer exportBuffer{};
    exportBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    exportBuffer.index = index;
    exportBuffer.flags = O_CLOEXEC;
    if (xioctl(fd_, VIDIOC_EXPBUF, &exportBuffer) == 0) {
      buffers_[index].dmabufFd = exportBuffer.fd;
    } else {
      dmabufSupported_ = false;
    }
#else
    dmabufSupported_ = false;
#endif

    if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
      const int error = -errno;
      releaseBuffersLocked();
      return error;
    }
  }

  // Derive buffers from the contiguous allocation base.
  uint32_t baseAddress = 0;
  v4l2_control address{.id = V4L2_CID_PADDR_Y, .value = 0};
  if (xioctl(fd_, VIDIOC_S_CTRL, &address) == 0 && address.value != 0 &&
      address.value != -1) {
    baseAddress = static_cast<uint32_t>(address.value);
    for (uint32_t index = 0; index < buffers_.size(); ++index) {
      buffers_[index].paddr =
          baseAddress + static_cast<uint32_t>(index * buffers_[index].length);
    }
  }

  if (!baseAddress) {
    ALOGW("capture buffers have no exported physical address; CPU fallback "
          "will be used");
  }

  width_ = captureFormat.fmt.pix.width;
  height_ = captureFormat.fmt.pix.height;
  format_ = captureFormat.fmt.pix.pixelformat;
  uint64_t abortValue;
  while (::read(abortFd_, &abortValue, sizeof(abortValue)) ==
         sizeof(abortValue)) {
  }
  return 0;
}

int ExynosPipeline::start() {
  std::lock_guard<std::mutex> guard(lock_);
  if (fd_ < 0 || buffers_.empty())
    return -ENODEV;
  if (streaming_)
    return 0;
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
    return -errno;
  streaming_ = true;
  return 0;
}

int ExynosPipeline::dequeue(Frame *frame) {
  if (!frame)
    return -EINVAL;

  int videoFd;
  int abortFd;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (!streaming_)
      return -ENODEV;
    videoFd = fd_;
    abortFd = abortFd_;
  }

  pollfd descriptors[2] = {
      {.fd = videoFd, .events = POLLIN | POLLERR, .revents = 0},
      {.fd = abortFd, .events = POLLIN, .revents = 0},
  };
  int result;
  do {
    result = poll(descriptors, 2, kDequeueTimeoutMs);
  } while (result < 0 && errno == EINTR);
  if (result == 0)
    return -ETIMEDOUT;
  if (result < 0)
    return -errno;
  if (descriptors[1].revents & POLLIN)
    return -ECANCELED;
  if (descriptors[0].revents & POLLERR)
    return -EIO;

  std::lock_guard<std::mutex> guard(lock_);
  if (!streaming_ || fd_ != videoFd)
    return -ECANCELED;

  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0)
    return -errno;
  if (buffer.index >= buffers_.size())
    return -EIO;

  auto &mapped = buffers_[buffer.index];
  frame->data = static_cast<uint8_t *>(mapped.ptr);
  frame->size = buffer.bytesused
                    ? std::min<size_t>(buffer.bytesused, mapped.length)
                    : mapped.length;
  frame->width = width_;
  frame->height = height_;
  frame->format = format_;
  frame->paddrY = mapped.paddr;
  frame->paddrCbcr = mapped.paddr ? mapped.paddr + width_ * height_ : 0;
  frame->dmabufFd = mapped.dmabufFd;
  frame->index = static_cast<int>(buffer.index);
  frame->timestampNs =
      buffer.timestamp.tv_sec || buffer.timestamp.tv_usec
          ? static_cast<int64_t>(buffer.timestamp.tv_sec) * 1000000000LL +
                static_cast<int64_t>(buffer.timestamp.tv_usec) * 1000LL
          : monotonicNs();
  return 0;
}

int ExynosPipeline::requeue(const Frame &frame) {
  std::lock_guard<std::mutex> guard(lock_);
  if (!streaming_ || frame.index < 0 ||
      static_cast<size_t>(frame.index) >= buffers_.size()) {
    return -EINVAL;
  }
  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  buffer.index = static_cast<uint32_t>(frame.index);
  return xioctl(fd_, VIDIOC_QBUF, &buffer) < 0 ? -errno : 0;
}

void ExynosPipeline::requestAbort() {
  std::lock_guard<std::mutex> guard(lock_);
  if (abortFd_ < 0)
    return;
  const uint64_t value = 1;
  if (::write(abortFd_, &value, sizeof(value)) < 0 && errno != EAGAIN) {
    ALOGW("eventfd abort failed: %s", strerror(errno));
  }
}

void ExynosPipeline::clearAbort() {
  std::lock_guard<std::mutex> guard(lock_);
  if (abortFd_ < 0)
    return;
  uint64_t value;
  while (::read(abortFd_, &value, sizeof(value)) == sizeof(value)) {
  }
}

void ExynosPipeline::releaseBuffersLocked() {
  for (auto &buffer : buffers_) {
    if (buffer.ptr && buffer.ptr != MAP_FAILED)
      munmap(buffer.ptr, buffer.length);
    if (buffer.dmabufFd >= 0)
      ::close(buffer.dmabufFd);
  }
  buffers_.clear();
  dmabufSupported_ = false;
}

int ExynosPipeline::stop() {
  std::lock_guard<std::mutex> guard(lock_);
  if (fd_ < 0)
    return 0;
  if (streaming_) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0 && errno != EINVAL) {
      ALOGW("VIDIOC_STREAMOFF failed: %s", strerror(errno));
    }
    streaming_ = false;
  }
  releaseBuffersLocked();

  v4l2_requestbuffers request{};
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  request.count = 0;
  (void)xioctl(fd_, VIDIOC_REQBUFS, &request);
  return 0;
}

void ExynosPipeline::close() {
  requestAbort();
  stop();
  std::lock_guard<std::mutex> guard(lock_);
  if (fd_ >= 0)
    ::close(fd_);
  if (abortFd_ >= 0)
    ::close(abortFd_);
  fd_ = -1;
  abortFd_ = -1;
  cameraId_ = -1;
  width_ = height_ = format_ = 0;
}

} // namespace smdk4x12
