#define LOG_TAG "Smdk4x12Camera3"

#include "SmdkCamera.h"

#include "ExynosCameraControls.h"
#include "InterleavedDecoder.h"
#include "JpegEncoder.h"
#include "sec_format.h"

#include <log/log.h>

#include "gralloc_priv.h"
#include "gralloc_usage.h"

#include <hardware/gralloc.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <utils/Timers.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <inttypes.h>
#include <unistd.h>

namespace smdk4x12 {
namespace {

constexpr int kFenceTimeoutMs = 3000;
constexpr auto kFlushTimeout = std::chrono::milliseconds(4000);

int closeDevice(hw_device_t *hardwareDevice) {
  if (!hardwareDevice)
    return -EINVAL;
  auto *device = reinterpret_cast<camera3_device_t *>(hardwareDevice);
  auto *camera = CameraDevice::from(device);
  if (!camera)
    return -EINVAL;
  camera->close();
  delete camera;
  return 0;
}

uint8_t captureIntentForTemplate(int type) {
  switch (type) {
  case CAMERA3_TEMPLATE_STILL_CAPTURE:
    return ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
  case CAMERA3_TEMPLATE_VIDEO_RECORD:
    return ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
  case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
    return ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
  case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
    return ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
  case CAMERA3_TEMPLATE_MANUAL:
    return ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
  default:
    return ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
  }
}

CropRegion cropForFrame(const CameraMetadata &settings,
                        const SensorProfile &profile, const Frame &frame) {
  int64_t left = 0;
  int64_t top = 0;
  int64_t right = profile.maxWidth;
  int64_t bottom = profile.maxHeight;

  const auto zoomEntry = settings.find(ANDROID_CONTROL_ZOOM_RATIO);
  const float requestedZoom =
      zoomEntry.count && std::isfinite(zoomEntry.data.f[0])
          ? std::clamp(zoomEntry.data.f[0], 1.0f, 4.0f)
          : 1.0f;
  if (requestedZoom > 1.0f) {
    const int64_t width =
        static_cast<int64_t>(profile.maxWidth / requestedZoom);
    const int64_t height =
        static_cast<int64_t>(profile.maxHeight / requestedZoom);
    left = (profile.maxWidth - width) / 2;
    top = (profile.maxHeight - height) / 2;
    right = left + width;
    bottom = top + height;
  } else {
    const auto cropEntry = settings.find(ANDROID_SCALER_CROP_REGION);
    if (cropEntry.count >= 4 && cropEntry.data.i32[2] > 0 &&
        cropEntry.data.i32[3] > 0) {
      left =
          std::clamp<int64_t>(cropEntry.data.i32[0], 0, profile.maxWidth - 2);
      top =
          std::clamp<int64_t>(cropEntry.data.i32[1], 0, profile.maxHeight - 2);
      right = std::clamp<int64_t>(left + cropEntry.data.i32[2], left + 2,
                                  profile.maxWidth);
      bottom = std::clamp<int64_t>(top + cropEntry.data.i32[3], top + 2,
                                   profile.maxHeight);
    }
  }

  CropRegion crop;
  crop.left =
      static_cast<uint32_t>(left * frame.width / profile.maxWidth) & ~1U;
  crop.top =
      static_cast<uint32_t>(top * frame.height / profile.maxHeight) & ~1U;
  uint32_t mappedRight =
      static_cast<uint32_t>(right * frame.width / profile.maxWidth) & ~1U;
  uint32_t mappedBottom =
      static_cast<uint32_t>(bottom * frame.height / profile.maxHeight) & ~1U;
  mappedRight = std::clamp(mappedRight, crop.left + 2, frame.width);
  mappedBottom = std::clamp(mappedBottom, crop.top + 2, frame.height);

  // Pre-5.0 FIMC requires a 16-pixel-aligned crop width.
  const uint32_t unalignedWidth = mappedRight - crop.left;
  crop.width = unalignedWidth & ~15U;
  if (crop.width < 16)
    crop.width = std::min<uint32_t>(16, frame.width & ~15U);
  const uint32_t centerX = crop.left + unalignedWidth / 2;
  crop.left = centerX > crop.width / 2 ? centerX - crop.width / 2 : 0;
  crop.left &= ~1U;
  if (crop.left + crop.width > frame.width)
    crop.left = (frame.width - crop.width) & ~1U;
  crop.height = (mappedBottom - crop.top) & ~1U;
  return crop;
}

} // namespace

CameraDevice *CameraDevice::from(const camera3_device_t *device) {
  return device ? static_cast<CameraDevice *>(device->priv) : nullptr;
}

CameraDevice::CameraDevice(int id, std::function<void()> onClosed)
    : id_(id), onClosed_(std::move(onClosed)) {
  sensor_ = std::make_unique<SensorController>(id_, &pipeline_);
  threeA_.reset(sensor_->profile().fixedFocus);
  initializeDefaultRequests();
}

CameraDevice::~CameraDevice() {
  close();
  for (size_t i = 1; i < defaultRequestPointers_.size(); ++i) {
    if (defaultRequestPointers_[i]) {
      defaultRequests_[i].unlock(defaultRequestPointers_[i]);
      defaultRequestPointers_[i] = nullptr;
    }
  }
}

int CameraDevice::open(const hw_module_t *module, camera3_device_t **output) {
  if (!module || !output)
    return -EINVAL;

  operations_.initialize = [](const camera3_device_t *device,
                              const camera3_callback_ops_t *callbacks) {
    return from(device)->initialize(callbacks);
  };
  operations_.configure_streams =
      [](const camera3_device_t *device,
         camera3_stream_configuration_t *configuration) {
        return from(device)->configureStreams(configuration);
      };
  operations_.register_stream_buffers = nullptr;
  operations_.construct_default_request_settings =
      [](const camera3_device_t *device, int type) {
        return from(device)->defaultRequest(type);
      };
  operations_.process_capture_request = [](const camera3_device_t *device,
                                           camera3_capture_request_t *request) {
    return from(device)->processCaptureRequest(request);
  };
  operations_.get_metadata_vendor_tag_ops = nullptr;
  operations_.dump = [](const camera3_device_t *device, int fd) {
    from(device)->dump(fd);
  };
  operations_.flush = [](const camera3_device_t *device) {
    return from(device)->flush();
  };

  device_.common.tag = HARDWARE_DEVICE_TAG;
  device_.common.version = CAMERA_DEVICE_API_VERSION_3_5;
  device_.common.module = const_cast<hw_module_t *>(module);
  device_.common.close = closeDevice;
  device_.ops = &operations_;
  device_.priv = this;
  *output = &device_;
  return 0;
}

int CameraDevice::initialize(const camera3_callback_ops_t *callbacks) {
  if (!callbacks || !callbacks->notify || !callbacks->process_capture_result)
    return -EINVAL;
  std::lock_guard<std::mutex> guard(lock_);
  if (state_ != State::Open)
    return -ENODEV;

  const int status = pipeline_.open(id_, sensor_->profile().fimcIs);
  if (status)
    return status;
  callbacks_ = callbacks;
  worker_ = std::thread(&CameraDevice::workerLoop, this);
  state_ = State::Initialized;
  return 0;
}

int CameraDevice::configureStreams(
    camera3_stream_configuration_t *configuration) {
  if (!configuration || !configuration->streams ||
      !configuration->num_streams ||
      configuration->operation_mode !=
          CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE) {
    return -EINVAL;
  }
  if (configuration->num_streams > 3)
    return -EINVAL;

  uint32_t processedStreams = 0;
  uint32_t blobStreams = 0;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  uint32_t blobWidth = 0;
  uint32_t blobHeight = 0;
  std::vector<camera3_stream_t *> streams;
  streams.reserve(configuration->num_streams);
  for (uint32_t i = 0; i < configuration->num_streams; ++i) {
    camera3_stream_t *stream = configuration->streams[i];
    if (!stream || !isSupportedStream(id_, *stream))
      return -EINVAL;
    if (stream->format == HAL_PIXEL_FORMAT_BLOB) {
      ++blobStreams;
      if (static_cast<uint64_t>(stream->width) * stream->height >
          static_cast<uint64_t>(blobWidth) * blobHeight) {
        blobWidth = stream->width;
        blobHeight = stream->height;
      }
    } else {
      ++processedStreams;
      // Size capture for continuous streams, not the still JPEG stream.
      if (static_cast<uint64_t>(stream->width) * stream->height >
          static_cast<uint64_t>(sourceWidth) * sourceHeight) {
        sourceWidth = stream->width;
        sourceHeight = stream->height;
      }
    }
    streams.push_back(stream);
  }
  if (processedStreams > 2 || blobStreams > 1)
    return -EINVAL;
  // JPEG-only sessions still require a sensor mode.
  if (!sourceWidth || !sourceHeight) {
    sourceWidth = blobWidth;
    sourceHeight = blobHeight;
  }

  bool wasConfigured;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ != State::Initialized && state_ != State::Configured)
      return -ENODEV;
    wasConfigured = state_ == State::Configured;
  }
  if (wasConfigured) {
    const int flushStatus = flush();
    if (flushStatus)
      return flushStatus;
  }

  processor_.reset();
  {
    std::lock_guard<std::mutex> guard(lock_);
    streams_.clear();
    sourceWidth_ = 0;
    sourceHeight_ = 0;
    configuredFps_ = 0;
    haveFrameNumber_ = false;
    state_ = State::Initialized;
  }

  // STREAMOFF leaves the old FIMC-IS window active. Reopen for every setup,
  // including a retry after a failed configuration.
  int status = pipeline_.open(id_, sensor_->profile().fimcIs);

  const int fps = (sourceWidth > 1920 || sourceHeight > 1080) ? 10 : 30;
  if (!status)
    status = pipeline_.configure(sourceWidth, sourceHeight,
                                 sensor_->profile().captureFormat, fps);
  if (!status)
    status = pipeline_.start();
  if (status)
    return status;
  sensor_->resetControlCache();
  sensor_->setPreviewSize(sourceWidth, sourceHeight);

  for (camera3_stream_t *stream : streams) {
    // The encoder can retain several input frames while MFC completes an
    // encode.  A two-buffer camera stream lets the producer and consumer
    // exhaust the queue and Camera3 then times out in getBufferLockedCommon.
    // JPEG remains single-shot; preview and video need pipeline headroom.
    const bool videoStream = (stream->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) != 0;
    stream->max_buffers = stream->format == HAL_PIXEL_FORMAT_BLOB ? 1
                         : videoStream ? 4 : 3;
    // CAMERA_OUTPUT selects contiguous ION; CPU_WRITE covers the converter fallback.
    stream->usage |= GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_SW_WRITE_OFTEN;
    // Resolve opaque buffers to NV12 for video and NV21 for preview.
    if (stream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      stream->format = videoStream
                           ? HAL_PIXEL_FORMAT_YCbCr_420_SP
                           : HAL_PIXEL_FORMAT_YCrCb_420_SP;
    }
    if (stream->format == HAL_PIXEL_FORMAT_BLOB) {
      stream->usage |= GRALLOC_USAGE_SW_WRITE_OFTEN;
    }
  }

  std::lock_guard<std::mutex> guard(lock_);
  streams_ = std::move(streams);
  sourceWidth_ = sourceWidth;
  sourceHeight_ = sourceHeight;
  configuredFps_ = fps;
  haveFrameNumber_ = false;
  state_ = State::Configured;
  return 0;
}

void CameraDevice::initializeDefaultRequests() {
  for (int type = CAMERA3_TEMPLATE_PREVIEW; type < CAMERA3_TEMPLATE_COUNT;
       ++type) {
    auto &metadata = defaultRequests_[type];
    const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    const uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
    const uint8_t aeTrigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    const uint8_t antibanding = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    const int32_t exposureCompensation = 0;
    const int32_t fps[] = {15,
                           type == CAMERA3_TEMPLATE_STILL_CAPTURE ? 15 : 30};
    const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    const uint8_t afMode =
        sensor_->profile().fixedFocus
            ? ANDROID_CONTROL_AF_MODE_OFF
            : (type == CAMERA3_TEMPLATE_VIDEO_RECORD
                   ? ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO
                   : ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE);
    const uint8_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    const uint8_t intent = captureIntentForTemplate(type);
    const uint8_t scene = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    const uint8_t effect = ANDROID_CONTROL_EFFECT_MODE_OFF;
    const uint8_t stabilization = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    const uint8_t jpegQuality =
        type == CAMERA3_TEMPLATE_STILL_CAPTURE ? 95 : 90;
    const uint8_t thumbnailQuality = 90;
    const int32_t thumbnailSize[] = {0, 0};
    const int32_t jpegOrientation = 0;
    const uint8_t faceDetect = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    const int32_t crop[] = {0, 0,
                            static_cast<int32_t>(sensor_->profile().maxWidth),
                            static_cast<int32_t>(sensor_->profile().maxHeight)};
    const float zoomRatio = 1.0f;
    const uint8_t aberration = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    const uint8_t edge = type == CAMERA3_TEMPLATE_STILL_CAPTURE
                             ? ANDROID_EDGE_MODE_HIGH_QUALITY
                             : ANDROID_EDGE_MODE_FAST;
    const uint8_t hotPixel = type == CAMERA3_TEMPLATE_STILL_CAPTURE
                                 ? ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY
                                 : ANDROID_HOT_PIXEL_MODE_FAST;
    const uint8_t noiseReduction =
        type == CAMERA3_TEMPLATE_STILL_CAPTURE
            ? ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY
            : ANDROID_NOISE_REDUCTION_MODE_FAST;
    const uint8_t shading = type == CAMERA3_TEMPLATE_STILL_CAPTURE
                                ? ANDROID_SHADING_MODE_HIGH_QUALITY
                                : ANDROID_SHADING_MODE_FAST;
    const uint8_t tonemap = type == CAMERA3_TEMPLATE_STILL_CAPTURE
                                ? ANDROID_TONEMAP_MODE_HIGH_QUALITY
                                : ANDROID_TONEMAP_MODE_FAST;
    const float aperture = 2.6f;
    const float focalLength = sensor_->profile().focalLength;
    const float filterDensity = 0.0f;
    const uint8_t opticalStabilization =
        ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;

    metadata.update(ANDROID_CONTROL_MODE, &controlMode, 1);
    metadata.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
    metadata.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);
    metadata.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &aeTrigger, 1);
    metadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibanding, 1);
    metadata.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                    &exposureCompensation, 1);
    metadata.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps, 2);
    metadata.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
    metadata.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);
    metadata.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);
    metadata.update(ANDROID_CONTROL_AF_TRIGGER, &afTrigger, 1);
    metadata.update(ANDROID_CONTROL_CAPTURE_INTENT, &intent, 1);
    metadata.update(ANDROID_CONTROL_SCENE_MODE, &scene, 1);
    metadata.update(ANDROID_CONTROL_EFFECT_MODE, &effect, 1);
    metadata.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &stabilization,
                    1);
    metadata.update(ANDROID_FLASH_MODE, &flashMode, 1);
    metadata.update(ANDROID_JPEG_QUALITY, &jpegQuality, 1);
    metadata.update(ANDROID_JPEG_THUMBNAIL_QUALITY, &thumbnailQuality, 1);
    metadata.update(ANDROID_JPEG_THUMBNAIL_SIZE, thumbnailSize, 2);
    metadata.update(ANDROID_JPEG_ORIENTATION, &jpegOrientation, 1);
    metadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetect, 1);
    metadata.update(ANDROID_SCALER_CROP_REGION, crop, 4);
    metadata.update(ANDROID_CONTROL_ZOOM_RATIO, &zoomRatio, 1);
    metadata.update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &aberration, 1);
    metadata.update(ANDROID_EDGE_MODE, &edge, 1);
    metadata.update(ANDROID_HOT_PIXEL_MODE, &hotPixel, 1);
    metadata.update(ANDROID_NOISE_REDUCTION_MODE, &noiseReduction, 1);
    metadata.update(ANDROID_SHADING_MODE, &shading, 1);
    metadata.update(ANDROID_TONEMAP_MODE, &tonemap, 1);
    metadata.update(ANDROID_LENS_APERTURE, &aperture, 1);
    metadata.update(ANDROID_LENS_FOCAL_LENGTH, &focalLength, 1);
    metadata.update(ANDROID_LENS_FILTER_DENSITY, &filterDensity, 1);
    metadata.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
                    &opticalStabilization, 1);
    defaultRequestPointers_[type] = metadata.getAndLock();
  }
}

const camera_metadata_t *CameraDevice::defaultRequest(int type) {
  if (type < CAMERA3_TEMPLATE_PREVIEW || type >= CAMERA3_TEMPLATE_COUNT ||
      type == CAMERA3_TEMPLATE_MANUAL)
    return nullptr;
  return defaultRequestPointers_[type];
}

int CameraDevice::processCaptureRequest(camera3_capture_request_t *request) {
  if (!request || request->input_buffer || !request->output_buffers ||
      !request->num_output_buffers) {
    return -EINVAL;
  }

  CaptureRequest pending;
  bool cancelImmediately = false;
  pending.frameNumber = request->frame_number;
  pending.outputBuffers.assign(request->output_buffers,
                               request->output_buffers +
                                   request->num_output_buffers);
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ != State::Configured)
      return -ENODEV;
    for (const auto &buffer : pending.outputBuffers) {
      if (!buffer.stream || !buffer.buffer || !*buffer.buffer ||
          std::find(streams_.begin(), streams_.end(), buffer.stream) ==
              streams_.end()) {
        return -EINVAL;
      }
    }
    if (haveFrameNumber_ && request->frame_number <= lastFrameNumber_)
      return -EINVAL;
    if (request->settings) {
      pending.settings = request->settings;
      lastSettings_ = pending.settings;
      // Do not repeat one-shot AF/AE triggers with null settings.
      const uint8_t afIdle = ANDROID_CONTROL_AF_TRIGGER_IDLE;
      const uint8_t aeIdle = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
      lastSettings_.update(ANDROID_CONTROL_AF_TRIGGER, &afIdle, 1);
      lastSettings_.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &aeIdle, 1);
    } else if (!lastSettings_.isEmpty()) {
      pending.settings = lastSettings_;
    } else {
      return -EINVAL;
    }
    pending.generation = generation_;
    lastFrameNumber_ = request->frame_number;
    haveFrameNumber_ = true;
    if (flushing_)
      cancelImmediately = true;
    else
      queue_.push_back(std::move(pending));
  }
  if (cancelImmediately) {
    // Complete requests accepted during a flush race with errors.
    returnResult(&pending, -ECANCELED, 0, false);
    return 0;
  }
  queueChanged_.notify_one();
  return 0;
}

int CameraDevice::waitAcquireFence(camera3_stream_buffer_t *buffer) {
  if (!buffer || buffer->acquire_fence < 0)
    return 0;
  const int fence = buffer->acquire_fence;
  buffer->acquire_fence = -1;
  const int result = sync_wait(fence, kFenceTimeoutMs);
  const int savedErrno = errno;
  ::close(fence);
  return result < 0 ? -savedErrno : 0;
}

int CameraDevice::writeJpeg(const Frame &frame, camera3_stream_buffer_t *output,
                            const CameraMetadata &settings,
                            const std::vector<uint8_t> &embeddedJpeg,
                            const CropRegion &crop) {
  auto *handle = private_handle_t::dynamicCast(*output->buffer);
  if (!handle || handle->format != HAL_PIXEL_FORMAT_BLOB || handle->width <= 0 ||
      static_cast<size_t>(handle->width) <= sizeof(camera3_jpeg_blob_t) ||
      handle->size < handle->width)
    return -ENOSPC;
  const size_t jpegCapacity =
      static_cast<size_t>(handle->width) - sizeof(camera3_jpeg_blob_t);
  std::vector<uint8_t> encoded;
  size_t hardwareSize = 0;
  const bool fullFrame = crop.left == 0 && crop.top == 0 &&
                         crop.width == frame.width &&
                         crop.height == frame.height;
  if (!embeddedJpeg.empty() && output->stream->width == frame.width &&
      output->stream->height == frame.height && fullFrame) {
    encoded = embeddedJpeg;
  } else {
    int quality = 95;
    const auto entry = settings.find(ANDROID_JPEG_QUALITY);
    if (entry.count)
      quality = entry.data.u8[0];
    if (fullFrame && frame.paddrY && handle->paddr) {
      if (!encodeJpegHardware(frame.paddrY, frame.size, frame.width,
                              frame.height, frame.format, quality,
                              static_cast<uint32_t>(handle->paddr),
                              jpegCapacity, &hardwareSize)) {
        ALOGW("hardware JPEG unavailable, falling back to libjpeg");
      }
    }
    if (!hardwareSize &&
        !encodeJpegCropped(frame.data, frame.size, frame.width, frame.height,
                           frame.format, crop.left, crop.top, crop.width,
                           crop.height, quality, &encoded)) {
      return -EIO;
    }
  }

  void *destination = nullptr;
  const android::Rect bounds(std::max(handle->width, 1),
                             std::max(handle->height, 1));
  const auto status = android::GraphicBufferMapper::get().lock(
      *output->buffer, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &destination);
  if (status != android::NO_ERROR || !destination)
    return -EIO;
  if (hardwareSize) {
    if (hardwareSize > jpegCapacity) {
      android::GraphicBufferMapper::get().unlock(*output->buffer);
      return -ENOSPC;
    }
    encoded.assign(static_cast<const uint8_t *>(destination),
                   static_cast<const uint8_t *>(destination) + hardwareSize);
  }

  int orientation = 0;
  const auto orientationEntry = settings.find(ANDROID_JPEG_ORIENTATION);
  if (orientationEntry.count)
    orientation = orientationEntry.data.i32[0];
  if (!addExifOrientation(orientation, &encoded)) {
    android::GraphicBufferMapper::get().unlock(*output->buffer);
    return -EINVAL;
  }
  if (encoded.size() > jpegCapacity) {
    android::GraphicBufferMapper::get().unlock(*output->buffer);
    return -ENOSPC;
  }

  memcpy(destination, encoded.data(), encoded.size());
  // Android 16's ImageReader reads the AIDL CameraBlob trailer: two uint32_t
  // fields. The legacy camera3_jpeg_blob_t has a uint16_t ID plus padding;
  // assigning only its ID leaves the upper half uninitialized and makes the
  // reader treat the entire multi-megabyte BLOB as JPEG data.
  static_assert(sizeof(camera3_jpeg_blob_t) == 2 * sizeof(uint32_t));
  auto *blob = static_cast<uint8_t *>(destination) + handle->width -
               sizeof(camera3_jpeg_blob_t);
  const uint32_t blobId = CAMERA3_JPEG_BLOB_ID;
  const uint32_t jpegSize = static_cast<uint32_t>(encoded.size());
  memcpy(blob, &blobId, sizeof(blobId));
  memcpy(blob + sizeof(blobId), &jpegSize, sizeof(jpegSize));
  android::GraphicBufferMapper::get().unlock(*output->buffer);
  return 0;
}

void CameraDevice::notifyShutter(uint32_t frameNumber, int64_t timestampNs) {
  camera3_notify_msg_t message{};
  message.type = CAMERA3_MSG_SHUTTER;
  message.message.shutter.frame_number = frameNumber;
  message.message.shutter.timestamp = timestampNs;
  callbacks_->notify(callbacks_, &message);
}

void CameraDevice::notifyError(uint32_t frameNumber,
                               camera3_error_msg_code_t code,
                               camera3_stream_t *stream) {
  camera3_notify_msg_t message{};
  message.type = CAMERA3_MSG_ERROR;
  message.message.error.frame_number = frameNumber;
  message.message.error.error_code = code;
  message.message.error.error_stream = stream;
  callbacks_->notify(callbacks_, &message);
}

void CameraDevice::returnResult(CaptureRequest *request, int status,
                                int64_t timestampNs, bool shutterSent) {
  if (!request || !callbacks_)
    return;

  if (status)
    notifyError(request->frameNumber, CAMERA3_MSG_ERROR_REQUEST);
  for (auto &buffer : request->outputBuffers) {
    if (buffer.acquire_fence >= 0) {
      ::close(buffer.acquire_fence);
      buffer.acquire_fence = -1;
    }
    if (status)
      buffer.status = CAMERA3_BUFFER_STATUS_ERROR;
    buffer.release_fence = -1;
  }

  CameraMetadata result;
  const camera_metadata_t *locked = nullptr;
  if (!status && shutterSent) {
    result = request->settings;
    result.update(ANDROID_SENSOR_TIMESTAMP, &timestampNs, 1);
    const uint8_t depth = 1;
    result.update(ANDROID_REQUEST_PIPELINE_DEPTH, &depth, 1);
    const float focusDistance = 0.0f;
    result.update(ANDROID_LENS_FOCUS_DISTANCE, &focusDistance, 1);
    const uint8_t flashState =
        !sensor_->profile().flash
            ? ANDROID_FLASH_STATE_UNAVAILABLE
            : (request->flashFired ? ANDROID_FLASH_STATE_FIRED
                                   : ANDROID_FLASH_STATE_READY);
    result.update(ANDROID_FLASH_STATE, &flashState, 1);
    threeA_.updateResult(result, *sensor_);
    const uint8_t lensState = threeA_.lensState();
    result.update(ANDROID_LENS_STATE, &lensState, 1);
    locked = result.getAndLock();
  }

  camera3_capture_result_t captureResult{};
  captureResult.frame_number = request->frameNumber;
  captureResult.result = locked;
  captureResult.num_output_buffers = request->outputBuffers.size();
  captureResult.output_buffers = request->outputBuffers.data();
  captureResult.partial_result = locked ? 1 : 0;
  callbacks_->process_capture_result(callbacks_, &captureResult);
  if (locked)
    result.unlock(locked);
}

int CameraDevice::processOne(CaptureRequest *request) {
  if (!request)
    return -EINVAL;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (request->generation != generation_ || flushing_)
      return -ECANCELED;
  }

  int status = sensor_->apply(request->settings);
  if (status)
    return status;
  threeA_.applyRequest(request->settings, *sensor_);

  const bool hasJpeg =
      std::any_of(request->outputBuffers.begin(), request->outputBuffers.end(),
                  [](const auto &buffer) {
                    return buffer.stream->format == HAL_PIXEL_FORMAT_BLOB;
                  });
  if (hasJpeg && sensor_->profile().kind == SensorKind::S5C73M3) {
    (void)pipeline_.setControl(V4L2_CID_CAMERA_CAPTURE, 0);
    (void)pipeline_.setControl(V4L2_CID_CAMERA_HYBRID_CAPTURE, 1);
  }

  // Pulse ISX012 torch and discard two pre-lit frames for still capture.
  const bool pulseFlash = hasJpeg &&
                          sensor_->profile().kind == SensorKind::ISX012 &&
                          sensor_->shouldFireCaptureFlash(request->settings);
  if (pulseFlash) {
    status = sensor_->setCaptureFlash(true);
    if (status)
      return status;
    for (int warmup = 0; warmup < 2; ++warmup) {
      Frame discarded{};
      status = pipeline_.dequeue(&discarded);
      if (status) {
        (void)sensor_->setCaptureFlash(false);
        return status;
      }
      status = pipeline_.requeue(discarded);
      if (status) {
        (void)sensor_->setCaptureFlash(false);
        return status;
      }
    }
  }

  Frame frame{};
  status = pipeline_.dequeue(&frame);
  if (pulseFlash) {
    (void)sensor_->setCaptureFlash(false);
    request->flashFired = !status;
  }
  if (status)
    return status;

  {
    std::lock_guard<std::mutex> guard(lock_);
    if (request->generation != generation_ || flushing_) {
      pipeline_.requeue(frame);
      return -ECANCELED;
    }
  }

  std::vector<uint8_t> embeddedJpeg;
  if (sensor_->profile().kind == SensorKind::S5C73M3) {
    InterleavedResult decoded;
    const int decodeStatus = decodeS5c73m3Interleaved(
        frame.data, frame.size, frame.width, frame.height, &decoded);
    if (decodeStatus) {
      pipeline_.requeue(frame);
      return decodeStatus;
    }
    threeA_.setEmbeddedAfStatus(decoded.autofocusStatus);
    if (decoded.decoded) {
      if (decoded.yuv.size() > frame.size) {
        pipeline_.requeue(frame);
        return -ENOSPC;
      }
      memcpy(frame.data, decoded.yuv.data(), decoded.yuv.size());
      frame.size = decoded.yuv.size();
      frame.format = V4L2_PIX_FMT_UYVY;
      embeddedJpeg = std::move(decoded.jpeg);
    } else {
      frame.format = V4L2_PIX_FMT_UYVY;
    }
  }

  notifyShutter(request->frameNumber, frame.timestampNs);
  const CropRegion crop =
      cropForFrame(request->settings, sensor_->profile(), frame);
  std::vector<int> bufferStatuses(request->outputBuffers.size(), 0);
  std::vector<camera3_stream_buffer_t *> processedOutputs;
  std::vector<size_t> processedIndices;
  for (size_t index = 0; index < request->outputBuffers.size(); ++index) {
    auto &buffer = request->outputBuffers[index];
    bufferStatuses[index] = waitAcquireFence(&buffer);
    if (bufferStatuses[index])
      continue;
    if (buffer.stream->format == HAL_PIXEL_FORMAT_BLOB) {
      bufferStatuses[index] =
          writeJpeg(frame, &buffer, request->settings, embeddedJpeg, crop);
    } else {
      processedOutputs.push_back(&buffer);
      processedIndices.push_back(index);
    }
  }

  const auto processedStatuses =
      processor_.writeMany(frame, processedOutputs, crop);
  for (size_t index = 0; index < processedStatuses.size(); ++index)
    bufferStatuses[processedIndices[index]] = processedStatuses[index];

  for (size_t index = 0; index < request->outputBuffers.size(); ++index) {
    auto &buffer = request->outputBuffers[index];
    const int bufferStatus = bufferStatuses[index];
    buffer.release_fence = -1;
    if (bufferStatus) {
      buffer.status = CAMERA3_BUFFER_STATUS_ERROR;
      notifyError(request->frameNumber, CAMERA3_MSG_ERROR_BUFFER,
                  buffer.stream);
    } else {
      buffer.status = CAMERA3_BUFFER_STATUS_OK;
    }
  }
  pipeline_.requeue(frame);
  returnResult(request, 0, frame.timestampNs, true);
  return 0;
}

void CameraDevice::workerLoop() {
  for (;;) {
    CaptureRequest request;
    {
      std::unique_lock<std::mutex> guard(lock_);
      queueChanged_.wait(guard, [&] { return exit_ || !queue_.empty(); });
      if (exit_ && queue_.empty())
        break;
      request = std::move(queue_.front());
      queue_.pop_front();
      ++activeRequests_;
    }

    const int status = processOne(&request);
    if (status)
      returnResult(&request, status, 0, false);

    {
      std::lock_guard<std::mutex> guard(lock_);
      --activeRequests_;
      if (!activeRequests_ && queue_.empty())
        drained_.notify_all();
    }
  }
}

int CameraDevice::flush() {
  std::deque<CaptureRequest> pending;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == State::Closed)
      return -ENODEV;
    flushing_ = true;
    ++generation_;
    pending.swap(queue_);
  }
  pipeline_.requestAbort();

  bool drained;
  {
    std::unique_lock<std::mutex> guard(lock_);
    drained = drained_.wait_for(guard, kFlushTimeout,
                                [&] { return activeRequests_ == 0; });
  }
  // Return the active request before flushed queued requests.
  for (auto &request : pending)
    returnResult(&request, -ECANCELED, 0, false);
  if (drained) {
    {
      std::lock_guard<std::mutex> guard(lock_);
      flushing_ = false;
    }
    pipeline_.clearAbort();
  }
  return drained ? 0 : -ETIMEDOUT;
}

void CameraDevice::dump(int fd) const {
  if (fd < 0)
    return;
  std::lock_guard<std::mutex> guard(lock_);
  dprintf(fd,
          "camera.smdk4x12 id=%d sensor=%s state=%d configured=%ux%u@%d "
          "queued=%zu active=%zu generation=%" PRIu64 " dmabuf=%s\n",
          id_, sensor_->profile().name, static_cast<int>(state_), sourceWidth_,
          sourceHeight_, configuredFps_, queue_.size(), activeRequests_,
          generation_, pipeline_.dmabufSupported() ? "yes" : "no");
}

void CameraDevice::close() {
  std::function<void()> onClosed;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == State::Closed)
      return;
  }
  (void)flush();
  {
    std::lock_guard<std::mutex> guard(lock_);
    exit_ = true;
  }
  pipeline_.requestAbort();
  queueChanged_.notify_all();
  if (worker_.joinable())
    worker_.join();
  processor_.reset();
  pipeline_.close();
  {
    std::lock_guard<std::mutex> guard(lock_);
    streams_.clear();
    callbacks_ = nullptr;
    state_ = State::Closed;
    onClosed = std::move(onClosed_);
  }
  if (onClosed)
    onClosed();
}

} // namespace smdk4x12
