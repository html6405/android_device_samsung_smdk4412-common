#define LOG_TAG "Smdk4x12StreamProcessor"

#include "StreamProcessor.h"

#include <log/log.h>

#include "SecFimc.h"
#include "gralloc_priv.h"

#include <hardware/gralloc.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <thread>
#include <utility>

namespace smdk4x12 {
namespace {

bool supportedDestinationFormat(int format) {
  return format == HAL_PIXEL_FORMAT_YCrCb_420_SP ||
         format == HAL_PIXEL_FORMAT_YCbCr_420_SP ||
         format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
         format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
}

uint8_t sampleLuma(const Frame &frame, uint32_t x, uint32_t y) {
  if (frame.format == V4L2_PIX_FMT_NV21 || frame.format == V4L2_PIX_FMT_NV12) {
    return frame.data[static_cast<size_t>(y) * frame.width + x];
  }
  if (frame.format == V4L2_PIX_FMT_UYVY) {
    const size_t pair = (static_cast<size_t>(y) * frame.width + (x & ~1U)) * 2;
    return frame.data[pair + (x & 1U ? 3 : 1)];
  }
  if (frame.format == V4L2_PIX_FMT_YUYV) {
    return frame.data[(static_cast<size_t>(y) * frame.width + x) * 2];
  }
  return 0;
}

void sampleChroma(const Frame &frame, uint32_t x, uint32_t y, uint8_t *u,
                  uint8_t *v) {
  x &= ~1U;
  y &= ~1U;
  if (frame.format == V4L2_PIX_FMT_NV21 || frame.format == V4L2_PIX_FMT_NV12) {
    const size_t offset = static_cast<size_t>(frame.width) * frame.height +
                          static_cast<size_t>(y / 2) * frame.width + x;
    if (frame.format == V4L2_PIX_FMT_NV21) {
      *v = frame.data[offset];
      *u = frame.data[offset + 1];
    } else {
      *u = frame.data[offset];
      *v = frame.data[offset + 1];
    }
    return;
  }
  const size_t offset = (static_cast<size_t>(y) * frame.width + x) * 2;
  if (frame.format == V4L2_PIX_FMT_UYVY) {
    *u = frame.data[offset];
    *v = frame.data[offset + 2];
  } else if (frame.format == V4L2_PIX_FMT_YUYV) {
    *u = frame.data[offset + 1];
    *v = frame.data[offset + 3];
  } else {
    *u = *v = 128;
  }
}

} // namespace

struct StreamProcessor::Lane {
  struct Configuration {
    uint32_t sourceWidth;
    uint32_t sourceHeight;
    uint32_t sourceFormat;
    uint32_t cropLeft;
    uint32_t cropTop;
    uint32_t cropWidth;
    uint32_t cropHeight;
    uint32_t destinationWidth;
    uint32_t destinationHeight;
    int destinationFormat;

    bool operator==(const Configuration &) const = default;
  };

  struct Completion {
    explicit Completion(size_t count) : results(count), remaining(count) {}

    void finish(size_t index, int status) {
      std::lock_guard<std::mutex> guard(lock);
      results[index] = status;
      if (--remaining == 0)
        changed.notify_one();
    }

    std::vector<int> wait() {
      std::unique_lock<std::mutex> guard(lock);
      changed.wait(guard, [this] { return remaining == 0; });
      return std::move(results);
    }

    std::mutex lock;
    std::condition_variable changed;
    std::vector<int> results;
    size_t remaining;
  };

  struct Work {
    Frame frame;
    camera3_stream_buffer_t *output = nullptr;
    CropRegion crop;
    Completion *completion = nullptr;
    size_t index = 0;
  };

  Lane(StreamProcessor *owner, SecFimc::FIMC_DEV device)
      : owner(owner), device(device), worker([this] { run(); }) {}

  ~Lane() {
    {
      std::lock_guard<std::mutex> guard(lock);
      stopping = true;
    }
    changed.notify_one();
    if (worker.joinable())
      worker.join();
    if (fimc && fimc->flagCreate())
      fimc->destroy();
  }

  void submit(const Frame &frame, camera3_stream_buffer_t *output,
              const CropRegion &crop, Completion *completion, size_t index) {
    Work work{.frame = frame,
              .output = output,
              .crop = crop,
              .completion = completion,
              .index = index};
    {
      std::lock_guard<std::mutex> guard(lock);
      if (stopping) {
        completion->finish(index, -ECANCELED);
        return;
      }
      queue.push_back(std::move(work));
    }
    changed.notify_one();
  }

  void run() {
    for (;;) {
      Work work;
      {
        std::unique_lock<std::mutex> guard(lock);
        changed.wait(guard, [this] { return stopping || !queue.empty(); });
        if (queue.empty()) {
          if (stopping)
            return;
          continue;
        }
        work = std::move(queue.front());
        queue.pop_front();
      }
      work.completion->finish(
          work.index,
          owner->writeOnLane(work.frame, work.output, work.crop, this));
    }
  }

  StreamProcessor *owner;
  SecFimc::FIMC_DEV device;
  std::unique_ptr<SecFimc> fimc;
  bool fimcUnavailable = false;
  bool configured = false;
  Configuration configuration{};
  std::mutex lock;
  std::condition_variable changed;
  std::deque<Work> queue;
  bool stopping = false;
  std::thread worker;
};

StreamProcessor::StreamProcessor() = default;

StreamProcessor::~StreamProcessor() { reset(); }

void StreamProcessor::reset() {
  std::unique_ptr<Lane> preview;
  std::unique_ptr<Lane> video;
  {
    std::lock_guard<std::mutex> guard(lanesLock_);
    preview = std::move(previewLane_);
    video = std::move(videoLane_);
  }
}

int StreamProcessor::write(const Frame &frame, camera3_stream_buffer_t *output,
                           const CropRegion &crop) {
  const auto results = writeMany(frame, {output}, crop);
  return results.empty() ? -EINVAL : results.front();
}

std::vector<int> StreamProcessor::writeMany(
    const Frame &frame, const std::vector<camera3_stream_buffer_t *> &outputs,
    const CropRegion &crop) {
  Lane::Completion completion(outputs.size());
  {
    std::lock_guard<std::mutex> guard(lanesLock_);
    for (size_t index = 0; index < outputs.size(); ++index) {
      auto *output = outputs[index];
      const bool video =
          output && output->stream &&
          (output->stream->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) != 0;
      auto &lane = video ? videoLane_ : previewLane_;
      if (!lane) {
        lane = std::make_unique<Lane>(this, video ? SecFimc::FIMC_DEV2
                                                  : SecFimc::FIMC_DEV1);
      }
      lane->submit(frame, output, crop, &completion, index);
    }
  }
  return completion.wait();
}

int StreamProcessor::writeOnLane(const Frame &frame,
                                 camera3_stream_buffer_t *output,
                                 const CropRegion &crop, Lane *lane) {
  if (!frame.data || !output || !output->stream || !output->buffer ||
      !*output->buffer) {
    return -EINVAL;
  }
  uint64_t requiredSourceSize;
  if (frame.format == V4L2_PIX_FMT_NV21 || frame.format == V4L2_PIX_FMT_NV12) {
    requiredSourceSize =
        static_cast<uint64_t>(frame.width) * frame.height * 3 / 2;
  } else if (frame.format == V4L2_PIX_FMT_UYVY ||
             frame.format == V4L2_PIX_FMT_YUYV) {
    requiredSourceSize = static_cast<uint64_t>(frame.width) * frame.height * 2;
  } else {
    return -EINVAL;
  }
  if (!frame.width || !frame.height || requiredSourceSize > frame.size)
    return -ENODATA;
  if (!crop.width || !crop.height || crop.left >= frame.width ||
      crop.top >= frame.height || crop.width > frame.width - crop.left ||
      crop.height > frame.height - crop.top)
    return -EINVAL;
  const auto *handle = private_handle_t::dynamicCast(*output->buffer);
  if (!handle || !supportedDestinationFormat(handle->format))
    return -EINVAL;

  if (frame.paddrY && handle->paddr) {
    const int status = writeWithFimc(frame, output, crop, lane);
    if (!status)
      return 0;
    ALOGW("FIMC conversion failed (%d), using CPU conversion", status);
  }
  return writeWithCpu(frame, output, crop);
}

int StreamProcessor::writeWithFimc(const Frame &frame,
                                   camera3_stream_buffer_t *output,
                                   const CropRegion &crop, Lane *lane) {
  auto *handle = private_handle_t::dynamicCast(*output->buffer);
  if (!handle || !handle->paddr || !lane)
    return -EINVAL;

  if (!lane->fimc && !lane->fimcUnavailable) {
    lane->fimc = std::make_unique<SecFimc>();
    if (!lane->fimc->create(lane->device, FIMC_OVLY_NOT_FIXED, 1)) {
      lane->fimc.reset();
      lane->fimcUnavailable = true;
      return -ENODEV;
    }
    ALOGI("FIMC%d output lane ready", static_cast<int>(lane->device));
  }
  if (!lane->fimc)
    return -ENODEV;

  const Lane::Configuration configuration{
      frame.width, frame.height, frame.format, crop.left, crop.top,
      crop.width, crop.height, output->stream->width, output->stream->height,
      handle->format};
  const auto resetFimc = [lane] {
    if (lane->fimc && lane->fimc->flagCreate())
      lane->fimc->destroy();
    lane->fimc.reset();
    lane->configured = false;
  };

  if (!lane->configured || lane->configuration != configuration) {
    if (!lane->fimc->streamOff()) {
      resetFimc();
      return -EIO;
    }

    unsigned sourceWidth = crop.width;
    unsigned sourceHeight = crop.height;
    unsigned destinationWidth = output->stream->width;
    unsigned destinationHeight = output->stream->height;
    if (!lane->fimc->setSrcParams(frame.width, frame.height, crop.left, crop.top,
                                  &sourceWidth, &sourceHeight,
                                  static_cast<int>(frame.format), true) ||
        !lane->fimc->setDstParams(output->stream->width,
                                  output->stream->height, 0, 0,
                                  &destinationWidth, &destinationHeight,
                                  handle->format, false)) {
      resetFimc();
      return -EINVAL;
    }
    lane->configuration = configuration;
    lane->configured = true;
  }

  if (!lane->fimc->setSrcPhyAddr(frame.paddrY, frame.paddrCbcr, 0,
                                  static_cast<int>(frame.format)) ||
      !lane->fimc->setDstPhyAddr(static_cast<unsigned>(handle->paddr)) ||
      !lane->fimc->streamOn() || !lane->fimc->queueBuffer(0)) {
    resetFimc();
    return -EIO;
  }
  int completedIndex = -1;
  if (!lane->fimc->dequeueBuffer(&completedIndex) || completedIndex != 0) {
    resetFimc();
    return -EIO;
  }
  return 0;
}

int StreamProcessor::writeWithCpu(const Frame &frame,
                                  camera3_stream_buffer_t *output,
                                  const CropRegion &crop) {
  auto *handle = private_handle_t::dynamicCast(*output->buffer);
  if (!handle)
    return -EINVAL;

  void *destination = nullptr;
  const android::Rect bounds(output->stream->width, output->stream->height);
  const auto status = android::GraphicBufferMapper::get().lock(
      *output->buffer, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &destination);
  if (status != android::NO_ERROR || !destination)
    return -EIO;

  const uint32_t width = output->stream->width;
  const uint32_t height = output->stream->height;
  const uint32_t stride =
      handle->stride > 0 ? static_cast<uint32_t>(handle->stride) : width;
  const size_t yBytes = static_cast<size_t>(stride) * height;
  // Honour gralloc's aligned chroma offset.
  const size_t chromaOffset =
      handle->uoffset > 0 ? static_cast<size_t>(handle->uoffset) : yBytes;
  const size_t required =
      chromaOffset + static_cast<size_t>(stride) * ((height + 1) / 2);
  if (handle->size <= 0 || required > static_cast<size_t>(handle->size)) {
    android::GraphicBufferMapper::get().unlock(*output->buffer);
    return -ENOSPC;
  }

  auto *yPlane = static_cast<uint8_t *>(destination);
  auto *chroma = yPlane + chromaOffset;
  for (uint32_t y = 0; y < height; ++y) {
    const uint32_t sourceY = std::min<uint32_t>(
        crop.top + crop.height - 1,
        crop.top + static_cast<uint64_t>(y) * crop.height / height);
    for (uint32_t x = 0; x < width; ++x) {
      const uint32_t sourceX = std::min<uint32_t>(
          crop.left + crop.width - 1,
          crop.left + static_cast<uint64_t>(x) * crop.width / width);
      yPlane[static_cast<size_t>(y) * stride + x] =
          sampleLuma(frame, sourceX, sourceY);
    }
  }

  const bool nv12 = handle->format == HAL_PIXEL_FORMAT_YCbCr_420_SP;
  for (uint32_t y = 0; y < height; y += 2) {
    const uint32_t sourceY = std::min<uint32_t>(
        crop.top + crop.height - 1,
        crop.top + static_cast<uint64_t>(y) * crop.height / height);
    for (uint32_t x = 0; x < width; x += 2) {
      const uint32_t sourceX = std::min<uint32_t>(
          crop.left + crop.width - 1,
          crop.left + static_cast<uint64_t>(x) * crop.width / width);
      uint8_t u;
      uint8_t v;
      sampleChroma(frame, sourceX, sourceY, &u, &v);
      const size_t offset = static_cast<size_t>(y / 2) * stride + x;
      chroma[offset] = nv12 ? u : v;
      if (x + 1 < stride)
        chroma[offset + 1] = nv12 ? v : u;
    }
  }

  android::GraphicBufferMapper::get().unlock(*output->buffer);
  return 0;
}

} // namespace smdk4x12
