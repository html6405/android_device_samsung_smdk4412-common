#define LOG_TAG "Smdk4x12Sensor"

#include "SensorController.h"

#include "ExynosCameraControls.h"
#include "ExynosPipeline.h"

#include <log/log.h>
#include <system/camera_metadata.h>

#include <algorithm>
#include <cerrno>

namespace smdk4x12 {

SensorController::SensorController(int cameraId, ExynosPipeline *pipeline)
    : pipeline_(pipeline), profile_(sensorProfile(cameraId)) {}

int SensorController::setBestEffort(uint32_t id, int value) {
  if (!pipeline_)
    return -ENODEV;
  const int status = pipeline_->setControl(id, value);
  if (status == -EINVAL || status == -ENOTTY) {
    ALOGV("%s does not implement control 0x%x", profile_.name, id);
    return 0;
  }
  return status;
}

int SensorController::setCached(uint32_t id, int value) {
  const auto previous = controlCache_.find(id);
  if (previous != controlCache_.end() && previous->second == value)
    return 0;
  const int status = setBestEffort(id, value);
  if (!status)
    controlCache_[id] = value;
  return status;
}

void SensorController::resetControlCache() {
  controlCache_.clear();
  lastAndroidAfMode_ = 0xff;
  touchPointValid_ = false;
}

void SensorController::setPreviewSize(uint32_t width, uint32_t height) {
  previewWidth_ = width;
  previewHeight_ = height;
  touchPointValid_ = false;
}

int SensorController::mapAf(uint8_t value) {
  switch (value) {
  case ANDROID_CONTROL_AF_MODE_MACRO:
    return FOCUS_MODE_MACRO;
  case ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO:
    return FOCUS_MODE_CONTINOUS_VIDEO;
  case ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE:
    return FOCUS_MODE_CONTINOUS_PICTURE;
  case ANDROID_CONTROL_AF_MODE_OFF:
    return FOCUS_MODE_INFINITY;
  default:
    return FOCUS_MODE_AUTO;
  }
}

int SensorController::mapAwb(uint8_t value) {
  switch (value) {
  case ANDROID_CONTROL_AWB_MODE_INCANDESCENT:
    return WHITE_BALANCE_INCANDESCENT;
  case ANDROID_CONTROL_AWB_MODE_FLUORESCENT:
    return WHITE_BALANCE_FLUORESCENT;
  case ANDROID_CONTROL_AWB_MODE_DAYLIGHT:
    return WHITE_BALANCE_SUNNY;
  case ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT:
    return WHITE_BALANCE_CLOUDY;
  default:
    return WHITE_BALANCE_AUTO;
  }
}

int SensorController::mapScene(uint8_t value) {
  switch (value) {
  case ANDROID_CONTROL_SCENE_MODE_PORTRAIT:
    return SCENE_MODE_PORTRAIT;
  case ANDROID_CONTROL_SCENE_MODE_LANDSCAPE:
    return SCENE_MODE_LANDSCAPE;
  case ANDROID_CONTROL_SCENE_MODE_NIGHT:
    return SCENE_MODE_NIGHTSHOT;
  case ANDROID_CONTROL_SCENE_MODE_SUNSET:
    return SCENE_MODE_SUNSET;
  case ANDROID_CONTROL_SCENE_MODE_SPORTS:
    return SCENE_MODE_SPORTS;
  case ANDROID_CONTROL_SCENE_MODE_BEACH:
  case ANDROID_CONTROL_SCENE_MODE_SNOW:
    return SCENE_MODE_BEACH_SNOW;
  case ANDROID_CONTROL_SCENE_MODE_FIREWORKS:
    return SCENE_MODE_FIREWORKS;
  case ANDROID_CONTROL_SCENE_MODE_CANDLELIGHT:
    return SCENE_MODE_CANDLE_LIGHT;
  default:
    return SCENE_MODE_NONE;
  }
}

int SensorController::mapEffect(uint8_t value) {
  switch (value) {
  case ANDROID_CONTROL_EFFECT_MODE_MONO:
    return IMAGE_EFFECT_BNW;
  case ANDROID_CONTROL_EFFECT_MODE_NEGATIVE:
    return IMAGE_EFFECT_NEGATIVE;
  case ANDROID_CONTROL_EFFECT_MODE_SEPIA:
    return IMAGE_EFFECT_SEPIA;
  case ANDROID_CONTROL_EFFECT_MODE_AQUA:
    return IMAGE_EFFECT_AQUA;
  default:
    return IMAGE_EFFECT_NONE;
  }
}

int SensorController::mapAntibanding(uint8_t value) {
  switch (value) {
  case ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ:
    return ANTI_BANDING_50HZ;
  case ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ:
    return ANTI_BANDING_60HZ;
  case ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF:
    return ANTI_BANDING_OFF;
  default:
    return ANTI_BANDING_AUTO;
  }
}

int SensorController::apply(const CameraMetadata &settings) {
  if (!pipeline_)
    return -ENODEV;
  int firstError = 0;
  auto apply = [&](uint32_t id, int value) {
    const int status = setCached(id, value);
    if (status && !firstError)
      firstError = status;
  };

  auto entry = settings.find(ANDROID_CONTROL_AF_MODE);
  if (entry.count && !profile_.fixedFocus) {
    const uint8_t androidMode = entry.data.u8[0];
    int driverMode = mapAf(androidMode);
    // ISX012 implements usable continuous AF through AUTO.
    if (profile_.kind == SensorKind::ISX012 &&
        (androidMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE ||
         androidMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO)) {
      driverMode = FOCUS_MODE_AUTO;
    }
    apply(V4L2_CID_CAMERA_FOCUS_MODE, driverMode);
    if (profile_.kind == SensorKind::ISX012 &&
        androidMode != lastAndroidAfMode_ &&
        (androidMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE ||
         androidMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO)) {
      (void)setBestEffort(V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_ON);
    }
    lastAndroidAfMode_ = androidMode;
  }
  entry = settings.find(ANDROID_CONTROL_AF_REGIONS);
  if (entry.count >= 5 && !profile_.fixedFocus && previewWidth_ &&
      previewHeight_) {
    touchPointValid_ = false;
    const size_t regionCount = entry.count / 5;
    size_t selected = 0;
    for (size_t i = 1; i < regionCount; ++i) {
      if (entry.data.i32[i * 5 + 4] > entry.data.i32[selected * 5 + 4])
        selected = i;
    }
    const int32_t *region = entry.data.i32 + selected * 5;
    if (region[4] > 0 && region[2] > region[0] && region[3] > region[1]) {
      const int64_t centerX =
          static_cast<int64_t>(region[0]) + (region[2] - region[0]) / 2;
      const int64_t centerY =
          static_cast<int64_t>(region[1]) + (region[3] - region[1]) / 2;
      // ISX012 expects preview-space coordinates with a 40-pixel margin.
      const int x = std::clamp<int>(centerX * previewWidth_ / profile_.maxWidth,
                                    40, std::max<int>(40, previewWidth_ - 40));
      const int y =
          std::clamp<int>(centerY * previewHeight_ / profile_.maxHeight, 40,
                          std::max<int>(40, previewHeight_ - 40));
      apply(V4L2_CID_CAMERA_OBJECT_POSITION_X, x);
      apply(V4L2_CID_CAMERA_OBJECT_POSITION_Y, y);
      touchPointValid_ = true;
    }
  }
  entry = settings.find(ANDROID_CONTROL_AWB_MODE);
  if (entry.count)
    apply(V4L2_CID_CAMERA_WHITE_BALANCE, mapAwb(entry.data.u8[0]));
  entry = settings.find(ANDROID_CONTROL_SCENE_MODE);
  if (entry.count)
    apply(V4L2_CID_CAMERA_SCENE_MODE, mapScene(entry.data.u8[0]));
  entry = settings.find(ANDROID_CONTROL_EFFECT_MODE);
  if (entry.count)
    apply(V4L2_CID_CAMERA_EFFECT, mapEffect(entry.data.u8[0]));
  entry = settings.find(ANDROID_CONTROL_AE_ANTIBANDING_MODE);
  if (entry.count) {
    apply(V4L2_CID_CAMERA_ANTI_BANDING, mapAntibanding(entry.data.u8[0]));
  }
  entry = settings.find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION);
  if (entry.count) {
    // External ISP sensors expose EV through legacy BRIGHTNESS.
    apply(V4L2_CID_CAMERA_BRIGHTNESS, std::clamp(entry.data.i32[0], -4, 4));
  }
  entry = settings.find(ANDROID_CONTROL_AE_LOCK);
  if (entry.count && profile_.aeLock) {
    apply(V4L2_CID_CAMERA_AE_LOCK_UNLOCK,
          entry.data.u8[0] ? AE_LOCK : AE_UNLOCK);
  }
  entry = settings.find(ANDROID_CONTROL_AWB_LOCK);
  if (entry.count && profile_.awbLock) {
    apply(V4L2_CID_CAMERA_AWB_LOCK_UNLOCK,
          entry.data.u8[0] ? AWB_LOCK : AWB_UNLOCK);
  }
  entry = settings.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE);
  if (entry.count >= 2) {
    apply(V4L2_CID_CAMERA_FRAME_RATE, std::clamp(entry.data.i32[1], 7, 30));
  }

  int flashMode = FLASH_MODE_OFF;
  entry = settings.find(ANDROID_FLASH_MODE);
  if (entry.count && entry.data.u8[0] == ANDROID_FLASH_MODE_TORCH) {
    flashMode = FLASH_MODE_TORCH;
  } else if (entry.count && entry.data.u8[0] == ANDROID_FLASH_MODE_SINGLE) {
    flashMode = FLASH_MODE_ON;
  } else {
    entry = settings.find(ANDROID_CONTROL_AE_MODE);
    if (entry.count) {
      if (entry.data.u8[0] == ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH) {
        flashMode = FLASH_MODE_ON;
      } else if (entry.data.u8[0] == ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH) {
        flashMode = FLASH_MODE_AUTO;
      }
    }
  }
  if (profile_.flash)
    apply(V4L2_CID_CAMERA_FLASH_MODE, flashMode);
  return firstError;
}

int SensorController::triggerAf(uint8_t trigger) {
  if (profile_.fixedFocus)
    return 0;
  if (trigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
    return setBestEffort(V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_OFF);
  }
  if (trigger == ANDROID_CONTROL_AF_TRIGGER_START) {
    // Cancel the passive scan before starting a new AF window.
    const int cancelStatus =
        setBestEffort(V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_OFF);
    if (cancelStatus)
      return cancelStatus;
    if (touchPointValid_) {
      const int touchStatus =
          setBestEffort(V4L2_CID_CAMERA_TOUCH_AF_START_STOP, TOUCH_AF_START);
      if (touchStatus)
        return touchStatus;
    }
    return setBestEffort(V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_ON);
  }
  return 0;
}

int SensorController::afStatus() {
  if (profile_.fixedFocus)
    return CAMERA_AF_STATUS_SUCCESS;
  int value = CAMERA_AF_STATUS_IN_PROGRESS;
  if (!pipeline_ ||
      pipeline_->getControl(V4L2_CID_CAMERA_AUTO_FOCUS_RESULT, &value) < 0) {
    return CAMERA_AF_STATUS_IN_PROGRESS;
  }
  if (profile_.kind == SensorKind::ISX012) {
    // Translate ISX012 AF_RESULT_* bit values.
    switch (value) {
    case 0x02: // AF_RESULT_SUCCESS
      return CAMERA_AF_STATUS_SUCCESS;
    case 0x01: // AF_RESULT_FAILED
      return CAMERA_AF_STATUS_FAIL;
    case 0x08: // AF_RESULT_DOING
    case 0x00: // AF_RESULT_NONE
    default:
      return CAMERA_AF_STATUS_IN_PROGRESS;
    }
  }
  return value;
}

int SensorController::setTorch(bool enabled) {
  if (!profile_.flash)
    return -ENOSYS;
  return setBestEffort(V4L2_CID_CAMERA_FLASH_MODE,
                       enabled ? FLASH_MODE_TORCH : FLASH_MODE_OFF);
}

bool SensorController::shouldFireCaptureFlash(const CameraMetadata &settings) {
  if (!profile_.flash)
    return false;

  auto entry = settings.find(ANDROID_FLASH_MODE);
  if (entry.count) {
    if (entry.data.u8[0] == ANDROID_FLASH_MODE_SINGLE)
      return true;
    // Torch mode needs no one-shot pulse.
    if (entry.data.u8[0] == ANDROID_FLASH_MODE_TORCH)
      return false;
  }

  entry = settings.find(ANDROID_CONTROL_AE_MODE);
  if (!entry.count)
    return false;
  if (entry.data.u8[0] == ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH)
    return true;
  if (entry.data.u8[0] != ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH &&
      entry.data.u8[0] != ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH_REDEYE) {
    return false;
  }

  // Query the sensor's calibrated flash decision.
  int required = 0;
  return pipeline_ &&
         pipeline_->getControl(V4L2_CID_CAMERA_CAPTURE, &required) == 0 &&
         required != 0;
}

int SensorController::setCaptureFlash(bool enabled) {
  if (!profile_.flash)
    return -ENOSYS;
  const int status = setBestEffort(V4L2_CID_CAMERA_FLASH_MODE,
                                   enabled ? FLASH_MODE_TORCH : FLASH_MODE_OFF);
  // Restore the cached flash mode after the pulse.
  controlCache_.erase(V4L2_CID_CAMERA_FLASH_MODE);
  return status;
}

} // namespace smdk4x12
