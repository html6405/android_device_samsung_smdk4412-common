#define LOG_TAG "Smdk4x12Metadata"

#include "SmdkCamera.h"

#include <hardware/gralloc.h>
#include <log/log.h>
#include <system/camera_metadata.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

namespace smdk4x12 {
namespace {

struct MetadataCache {
  std::once_flag once;
  CameraMetadata metadata;
  const camera_metadata_t *locked = nullptr;
};

std::array<MetadataCache, 2> gCaches;

constexpr int64_t kPreviewDuration = 33333333LL;
constexpr int64_t kLargeImageDuration = 100000000LL;
constexpr int64_t kJpegStallDuration = 250000000LL;

template <typename T, size_t N>
void update(CameraMetadata *metadata, uint32_t tag, const T (&values)[N]) {
  metadata->update(tag, values, N);
}

void appendStreamMetadata(CameraMetadata *metadata,
                          const SensorProfile &profile) {
  std::vector<int32_t> configurations;
  std::vector<int64_t> minimumDurations;
  std::vector<int64_t> stallDurations;
  constexpr int32_t formats[] = {
      HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
      HAL_PIXEL_FORMAT_YCbCr_420_888,
      HAL_PIXEL_FORMAT_BLOB,
  };

  for (int32_t format : formats) {
    for (const Size &size : profile.outputSizes) {
      configurations.insert(
          configurations.end(),
          {format, static_cast<int32_t>(size.width),
           static_cast<int32_t>(size.height),
           ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT});
      const int64_t frameDuration = size.width > 1920 || size.height > 1080
                                        ? kLargeImageDuration
                                        : kPreviewDuration;
      minimumDurations.insert(minimumDurations.end(),
                              {format, size.width, size.height, frameDuration});
      stallDurations.insert(
          stallDurations.end(),
          {format, size.width, size.height,
           format == HAL_PIXEL_FORMAT_BLOB ? kJpegStallDuration : 0});
    }
  }
  metadata->update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                   configurations.data(), configurations.size());
  metadata->update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                   minimumDurations.data(), minimumDurations.size());
  metadata->update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
                   stallDurations.data(), stallDurations.size());
}

void buildStaticMetadata(int id, MetadataCache *cache) {
  auto &metadata = cache->metadata;
  const auto &profile = sensorProfile(id);

  const uint8_t hardwareLevel = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;
  metadata.update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &hardwareLevel, 1);
  const uint8_t capability =
      ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE;
  metadata.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &capability, 1);

  const uint8_t lensFacing =
      profile.front ? ANDROID_LENS_FACING_FRONT : ANDROID_LENS_FACING_BACK;
  metadata.update(ANDROID_LENS_FACING, &lensFacing, 1);
  const int32_t sensorOrientation = profile.orientation;
  metadata.update(ANDROID_SENSOR_ORIENTATION, &sensorOrientation, 1);
  const int32_t pixelArray[] = {static_cast<int32_t>(profile.maxWidth),
                                static_cast<int32_t>(profile.maxHeight)};
  update(&metadata, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixelArray);
  const int32_t activeArray[] = {0, 0, static_cast<int32_t>(profile.maxWidth),
                                 static_cast<int32_t>(profile.maxHeight)};
  update(&metadata, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, activeArray);
  update(&metadata, ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
         activeArray);
  const float physicalSize[] = {profile.physicalWidth, profile.physicalHeight};
  update(&metadata, ANDROID_SENSOR_INFO_PHYSICAL_SIZE, physicalSize);
  const int64_t maxFrameDuration = kLargeImageDuration;
  metadata.update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, &maxFrameDuration, 1);
  const uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
  metadata.update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &timestampSource, 1);
  const int32_t testPattern = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
  metadata.update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, &testPattern, 1);

  metadata.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                  &profile.focalLength, 1);
  const float aperture = 2.6f;
  metadata.update(ANDROID_LENS_INFO_AVAILABLE_APERTURES, &aperture, 1);
  const float filterDensity = 0.0f;
  metadata.update(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, &filterDensity,
                  1);
  const float minimumFocusDistance = profile.fixedFocus ? 0.0f : 6.67f;
  metadata.update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
                  &minimumFocusDistance, 1);
  const float hyperfocalDistance = profile.fixedFocus ? 0.0f : 0.2f;
  metadata.update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, &hyperfocalDistance,
                  1);
  const uint8_t focusCalibration =
      profile.fixedFocus
          ? ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_CALIBRATED
          : ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_APPROXIMATE;
  metadata.update(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
                  &focusCalibration, 1);
  const uint8_t opticalStabilization =
      ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
  metadata.update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                  &opticalStabilization, 1);

  const uint8_t flashAvailable = profile.flash
                                     ? ANDROID_FLASH_INFO_AVAILABLE_TRUE
                                     : ANDROID_FLASH_INFO_AVAILABLE_FALSE;
  metadata.update(ANDROID_FLASH_INFO_AVAILABLE, &flashAvailable, 1);

  const uint8_t controlModes[] = {ANDROID_CONTROL_MODE_AUTO,
                                  ANDROID_CONTROL_MODE_USE_SCENE_MODE};
  update(&metadata, ANDROID_CONTROL_AVAILABLE_MODES, controlModes);
  const uint8_t sceneModes[] = {
      ANDROID_CONTROL_SCENE_MODE_DISABLED,  ANDROID_CONTROL_SCENE_MODE_PORTRAIT,
      ANDROID_CONTROL_SCENE_MODE_LANDSCAPE, ANDROID_CONTROL_SCENE_MODE_NIGHT,
      ANDROID_CONTROL_SCENE_MODE_SPORTS,    ANDROID_CONTROL_SCENE_MODE_SUNSET,
  };
  update(&metadata, ANDROID_CONTROL_AVAILABLE_SCENE_MODES, sceneModes);
  const uint8_t sceneAfMode = profile.fixedFocus
                                  ? ANDROID_CONTROL_AF_MODE_OFF
                                  : ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
  const uint8_t sceneOverrides[] = {
      ANDROID_CONTROL_AE_MODE_ON, ANDROID_CONTROL_AWB_MODE_AUTO, sceneAfMode,
      ANDROID_CONTROL_AE_MODE_ON, ANDROID_CONTROL_AWB_MODE_AUTO, sceneAfMode,
      ANDROID_CONTROL_AE_MODE_ON, ANDROID_CONTROL_AWB_MODE_AUTO, sceneAfMode,
      ANDROID_CONTROL_AE_MODE_ON, ANDROID_CONTROL_AWB_MODE_AUTO, sceneAfMode,
      ANDROID_CONTROL_AE_MODE_ON, ANDROID_CONTROL_AWB_MODE_AUTO, sceneAfMode,
      ANDROID_CONTROL_AE_MODE_ON, ANDROID_CONTROL_AWB_MODE_AUTO, sceneAfMode,
  };
  update(&metadata, ANDROID_CONTROL_SCENE_MODE_OVERRIDES, sceneOverrides);
  const uint8_t effects[] = {
      ANDROID_CONTROL_EFFECT_MODE_OFF,      ANDROID_CONTROL_EFFECT_MODE_MONO,
      ANDROID_CONTROL_EFFECT_MODE_NEGATIVE, ANDROID_CONTROL_EFFECT_MODE_SEPIA,
      ANDROID_CONTROL_EFFECT_MODE_AQUA,
  };
  update(&metadata, ANDROID_CONTROL_AVAILABLE_EFFECTS, effects);
  const uint8_t antibanding[] = {
      ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF,
      ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ,
      ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ,
      ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO,
  };
  update(&metadata, ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
         antibanding);
  const uint8_t aeModesWithFlash[] = {
      ANDROID_CONTROL_AE_MODE_ON,
      ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH,
      ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH,
  };
  const uint8_t aeModes[] = {ANDROID_CONTROL_AE_MODE_ON};
  if (profile.flash) {
    update(&metadata, ANDROID_CONTROL_AE_AVAILABLE_MODES, aeModesWithFlash);
  } else {
    update(&metadata, ANDROID_CONTROL_AE_AVAILABLE_MODES, aeModes);
  }
  const uint8_t awbModes[] = {
      ANDROID_CONTROL_AWB_MODE_AUTO,
      ANDROID_CONTROL_AWB_MODE_INCANDESCENT,
      ANDROID_CONTROL_AWB_MODE_FLUORESCENT,
      ANDROID_CONTROL_AWB_MODE_DAYLIGHT,
      ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT,
  };
  update(&metadata, ANDROID_CONTROL_AWB_AVAILABLE_MODES, awbModes);
  const uint8_t fixedAfModes[] = {ANDROID_CONTROL_AF_MODE_OFF};
  const uint8_t afModes[] = {
      ANDROID_CONTROL_AF_MODE_OFF,
      ANDROID_CONTROL_AF_MODE_AUTO,
      ANDROID_CONTROL_AF_MODE_MACRO,
      ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO,
      ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
  };
  if (profile.fixedFocus) {
    update(&metadata, ANDROID_CONTROL_AF_AVAILABLE_MODES, fixedAfModes);
  } else {
    update(&metadata, ANDROID_CONTROL_AF_AVAILABLE_MODES, afModes);
  }
  const int32_t compensationRange[] = {-4, 4};
  update(&metadata, ANDROID_CONTROL_AE_COMPENSATION_RANGE, compensationRange);
  const camera_metadata_rational_t compensationStep{1, 2};
  metadata.update(ANDROID_CONTROL_AE_COMPENSATION_STEP, &compensationStep, 1);
  const int32_t fpsRanges[] = {15, 15, 15, 30, 30, 30};
  update(&metadata, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, fpsRanges);
  const uint8_t aeLock = profile.aeLock
                             ? ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE
                             : ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
  const uint8_t awbLock = profile.awbLock
                              ? ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE
                              : ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
  metadata.update(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &aeLock, 1);
  metadata.update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awbLock, 1);
  // Region order: AE, AWB, AF.
  const int32_t maxRegions[] = {0, 0, profile.fixedFocus ? 0 : 1};
  update(&metadata, ANDROID_CONTROL_MAX_REGIONS, maxRegions);
  const uint8_t videoStabilization =
      ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
  metadata.update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
                  &videoStabilization, 1);

  appendStreamMetadata(&metadata, profile);
  const float maxDigitalZoom = 4.0f;
  metadata.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxDigitalZoom,
                  1);
  const float zoomRatioRange[] = {1.0f, maxDigitalZoom};
  update(&metadata, ANDROID_CONTROL_ZOOM_RATIO_RANGE, zoomRatioRange);
  const uint8_t croppingType = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
  metadata.update(ANDROID_SCALER_CROPPING_TYPE, &croppingType, 1);

  const int32_t maxOutputStreams[] = {0, 2, 1};
  update(&metadata, ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, maxOutputStreams);
  const int32_t maxInputStreams = 0;
  metadata.update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &maxInputStreams, 1);
  const int32_t partialResultCount = 1;
  metadata.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partialResultCount, 1);
  const uint8_t pipelineDepth = 4;
  metadata.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &pipelineDepth, 1);

  // Exynos JPEG currently emits no EXIF thumbnail.
  const int32_t thumbnailSizes[] = {0, 0};
  update(&metadata, ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, thumbnailSizes);
  const int32_t jpegMaxSize =
      static_cast<int32_t>(profile.maxWidth * profile.maxHeight * 2);
  metadata.update(ANDROID_JPEG_MAX_SIZE, &jpegMaxSize, 1);

  const uint8_t aberration = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
  metadata.update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
                  &aberration, 1);
  const uint8_t edge[] = {ANDROID_EDGE_MODE_OFF, ANDROID_EDGE_MODE_FAST,
                          ANDROID_EDGE_MODE_HIGH_QUALITY};
  update(&metadata, ANDROID_EDGE_AVAILABLE_EDGE_MODES, edge);
  const uint8_t hotPixel[] = {ANDROID_HOT_PIXEL_MODE_OFF,
                              ANDROID_HOT_PIXEL_MODE_FAST,
                              ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY};
  update(&metadata, ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, hotPixel);
  const uint8_t noiseReduction[] = {ANDROID_NOISE_REDUCTION_MODE_OFF,
                                    ANDROID_NOISE_REDUCTION_MODE_FAST,
                                    ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY};
  update(&metadata, ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
         noiseReduction);
  const uint8_t shading[] = {ANDROID_SHADING_MODE_OFF,
                             ANDROID_SHADING_MODE_FAST,
                             ANDROID_SHADING_MODE_HIGH_QUALITY};
  update(&metadata, ANDROID_SHADING_AVAILABLE_MODES, shading);
  const uint8_t tonemap[] = {ANDROID_TONEMAP_MODE_FAST,
                             ANDROID_TONEMAP_MODE_HIGH_QUALITY};
  update(&metadata, ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, tonemap);
  const uint8_t faceDetect = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
  metadata.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                  &faceDetect, 1);
  const int32_t maxFaces = 0;
  metadata.update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &maxFaces, 1);
  const uint8_t hotPixelMap = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
  metadata.update(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
                  &hotPixelMap, 1);
  const uint8_t lensShadingMap = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
  metadata.update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
                  &lensShadingMap, 1);
  const int32_t maxLatency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
  metadata.update(ANDROID_SYNC_MAX_LATENCY, &maxLatency, 1);

  const int32_t requestKeys[] = {
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
      ANDROID_CONTROL_AE_ANTIBANDING_MODE,
      ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
      ANDROID_CONTROL_AE_LOCK,
      ANDROID_CONTROL_AE_MODE,
      ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
      ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
      ANDROID_CONTROL_AF_MODE,
      ANDROID_CONTROL_AF_REGIONS,
      ANDROID_CONTROL_AF_TRIGGER,
      ANDROID_CONTROL_AWB_LOCK,
      ANDROID_CONTROL_AWB_MODE,
      ANDROID_CONTROL_CAPTURE_INTENT,
      ANDROID_CONTROL_EFFECT_MODE,
      ANDROID_CONTROL_MODE,
      ANDROID_CONTROL_SCENE_MODE,
      ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
      ANDROID_CONTROL_ZOOM_RATIO,
      ANDROID_EDGE_MODE,
      ANDROID_FLASH_MODE,
      ANDROID_HOT_PIXEL_MODE,
      ANDROID_JPEG_ORIENTATION,
      ANDROID_JPEG_QUALITY,
      ANDROID_JPEG_THUMBNAIL_QUALITY,
      ANDROID_JPEG_THUMBNAIL_SIZE,
      ANDROID_LENS_APERTURE,
      ANDROID_LENS_FILTER_DENSITY,
      ANDROID_LENS_FOCAL_LENGTH,
      ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
      ANDROID_NOISE_REDUCTION_MODE,
      ANDROID_SCALER_CROP_REGION,
      ANDROID_SHADING_MODE,
      ANDROID_STATISTICS_FACE_DETECT_MODE,
      ANDROID_TONEMAP_MODE,
  };
  update(&metadata, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, requestKeys);

  const int32_t resultKeys[] = {
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
      ANDROID_CONTROL_AE_ANTIBANDING_MODE,
      ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
      ANDROID_CONTROL_AE_LOCK,
      ANDROID_CONTROL_AE_MODE,
      ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
      ANDROID_CONTROL_AE_STATE,
      ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
      ANDROID_CONTROL_AF_MODE,
      ANDROID_CONTROL_AF_REGIONS,
      ANDROID_CONTROL_AF_STATE,
      ANDROID_CONTROL_AF_TRIGGER,
      ANDROID_CONTROL_AWB_LOCK,
      ANDROID_CONTROL_AWB_MODE,
      ANDROID_CONTROL_AWB_STATE,
      ANDROID_CONTROL_CAPTURE_INTENT,
      ANDROID_CONTROL_EFFECT_MODE,
      ANDROID_CONTROL_MODE,
      ANDROID_CONTROL_SCENE_MODE,
      ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
      ANDROID_CONTROL_ZOOM_RATIO,
      ANDROID_EDGE_MODE,
      ANDROID_FLASH_MODE,
      ANDROID_FLASH_STATE,
      ANDROID_HOT_PIXEL_MODE,
      ANDROID_JPEG_ORIENTATION,
      ANDROID_JPEG_QUALITY,
      ANDROID_JPEG_THUMBNAIL_QUALITY,
      ANDROID_JPEG_THUMBNAIL_SIZE,
      ANDROID_LENS_APERTURE,
      ANDROID_LENS_FILTER_DENSITY,
      ANDROID_LENS_FOCAL_LENGTH,
      ANDROID_LENS_FOCUS_DISTANCE,
      ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
      ANDROID_LENS_STATE,
      ANDROID_NOISE_REDUCTION_MODE,
      ANDROID_REQUEST_PIPELINE_DEPTH,
      ANDROID_SCALER_CROP_REGION,
      ANDROID_SENSOR_TIMESTAMP,
      ANDROID_SHADING_MODE,
      ANDROID_STATISTICS_FACE_DETECT_MODE,
      ANDROID_TONEMAP_MODE,
  };
  update(&metadata, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, resultKeys);

  const int32_t characteristicKeys[] = {
      ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
      ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
      ANDROID_CONTROL_AE_AVAILABLE_MODES,
      ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
      ANDROID_CONTROL_AE_COMPENSATION_RANGE,
      ANDROID_CONTROL_AE_COMPENSATION_STEP,
      ANDROID_CONTROL_AE_LOCK_AVAILABLE,
      ANDROID_CONTROL_AF_AVAILABLE_MODES,
      ANDROID_CONTROL_AVAILABLE_EFFECTS,
      ANDROID_CONTROL_AVAILABLE_MODES,
      ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
      ANDROID_CONTROL_SCENE_MODE_OVERRIDES,
      ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
      ANDROID_CONTROL_AWB_AVAILABLE_MODES,
      ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
      ANDROID_CONTROL_MAX_REGIONS,
      ANDROID_CONTROL_ZOOM_RATIO_RANGE,
      ANDROID_EDGE_AVAILABLE_EDGE_MODES,
      ANDROID_FLASH_INFO_AVAILABLE,
      ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
      ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
      ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
      ANDROID_JPEG_MAX_SIZE,
      ANDROID_LENS_FACING,
      ANDROID_LENS_INFO_AVAILABLE_APERTURES,
      ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
      ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
      ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
      ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
      ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
      ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
      ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
      ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
      ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
      ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
      ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
      ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
      ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
      ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
      ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
      ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
      ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
      ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
      ANDROID_SCALER_CROPPING_TYPE,
      ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
      ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
      ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
      ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
      ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
      ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
      ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
      ANDROID_SENSOR_ORIENTATION,
      ANDROID_SHADING_AVAILABLE_MODES,
      ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
      ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
      ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
      ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
      ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
      ANDROID_SYNC_MAX_LATENCY,
  };
  update(&metadata, ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
         characteristicKeys);

  cache->locked = metadata.getAndLock();
  const auto validation =
      validate_camera_metadata_structure(cache->locked, nullptr);
  if (validation != 0) {
    ALOGE("static metadata validation failed for camera %d: %d", id,
          validation);
  }
}

} // namespace

int facing(int id) {
  return sensorProfile(id).front ? CAMERA_FACING_FRONT : CAMERA_FACING_BACK;
}

int orientation(int id) { return sensorProfile(id).orientation; }

bool isSupportedStream(int id, const camera3_stream_t &stream) {
  if (stream.stream_type != CAMERA3_STREAM_OUTPUT ||
      stream.rotation != CAMERA3_STREAM_ROTATION_0) {
    return false;
  }
  if (stream.format != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
      stream.format != HAL_PIXEL_FORMAT_YCbCr_420_888 &&
      stream.format != HAL_PIXEL_FORMAT_BLOB) {
    return false;
  }
  const auto &sizes = sensorProfile(id).outputSizes;
  return std::any_of(sizes.begin(), sizes.end(), [&](const Size &size) {
    return size.width == stream.width && size.height == stream.height;
  });
}

const camera_metadata_t *staticMetadata(int id) {
  if (id < 0 || id >= static_cast<int>(gCaches.size()))
    return nullptr;
  auto &cache = gCaches[id];
  std::call_once(cache.once, [&] { buildStaticMetadata(id, &cache); });
  return cache.locked;
}

} // namespace smdk4x12
