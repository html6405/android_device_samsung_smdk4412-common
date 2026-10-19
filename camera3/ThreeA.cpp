#include "ThreeA.h"

#include "ExynosCameraControls.h"
#include "SensorController.h"

#include <system/camera_metadata.h>

namespace smdk4x12 {

void ThreeA::reset(bool fixedFocus) {
  fixedFocus_ = fixedFocus;
  afMode_ = ANDROID_CONTROL_AF_MODE_OFF;
  afState_ = ANDROID_CONTROL_AF_STATE_INACTIVE;
  aeState_ = ANDROID_CONTROL_AE_STATE_CONVERGED;
  awbState_ = ANDROID_CONTROL_AWB_STATE_CONVERGED;
  embeddedAfStatus_ = -1;
  afScanFrames_ = 0;
  aePrecaptureFrames_ = 0;
  awbSearchFrames_ = 0;
}

void ThreeA::applyRequest(const CameraMetadata &settings,
                          SensorController &sensor) {
  auto entry = settings.find(ANDROID_CONTROL_AF_MODE);
  const uint8_t requestedAfMode =
      entry.count ? entry.data.u8[0] : ANDROID_CONTROL_AF_MODE_OFF;
  if (fixedFocus_) {
    afMode_ = ANDROID_CONTROL_AF_MODE_OFF;
    afState_ = ANDROID_CONTROL_AF_STATE_INACTIVE;
    afScanFrames_ = 0;
  } else if (requestedAfMode != afMode_) {
    afMode_ = requestedAfMode;
    afState_ = (afMode_ == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE ||
                afMode_ == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO)
                   ? ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN
                   : ANDROID_CONTROL_AF_STATE_INACTIVE;
    afScanFrames_ = 0;
  }

  entry = settings.find(ANDROID_CONTROL_AF_TRIGGER);
  const uint8_t trigger =
      entry.count ? entry.data.u8[0] : ANDROID_CONTROL_AF_TRIGGER_IDLE;
  if (!fixedFocus_ && trigger == ANDROID_CONTROL_AF_TRIGGER_START) {
    sensor.triggerAf(trigger);
    afState_ = ANDROID_CONTROL_AF_STATE_ACTIVE_SCAN;
    afScanFrames_ = 0;
  } else if (!fixedFocus_ && trigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
    sensor.triggerAf(trigger);
    afState_ = ANDROID_CONTROL_AF_STATE_INACTIVE;
    afScanFrames_ = 0;
  }

  entry = settings.find(ANDROID_CONTROL_AE_MODE);
  const uint8_t aeMode =
      entry.count ? entry.data.u8[0] : ANDROID_CONTROL_AE_MODE_ON;
  entry = settings.find(ANDROID_CONTROL_AE_LOCK);
  const bool aeLocked =
      entry.count && entry.data.u8[0] == ANDROID_CONTROL_AE_LOCK_ON;
  entry = settings.find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER);
  const uint8_t aeTrigger = entry.count
                                ? entry.data.u8[0]
                                : ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
  if (aeMode == ANDROID_CONTROL_AE_MODE_OFF) {
    aeState_ = ANDROID_CONTROL_AE_STATE_INACTIVE;
    aePrecaptureFrames_ = 0;
  } else if (aeLocked) {
    aeState_ = ANDROID_CONTROL_AE_STATE_LOCKED;
    aePrecaptureFrames_ = 0;
  } else if (aeTrigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
    aeState_ = ANDROID_CONTROL_AE_STATE_PRECAPTURE;
    aePrecaptureFrames_ = 2;
  } else if (aeTrigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_CANCEL) {
    aeState_ = ANDROID_CONTROL_AE_STATE_CONVERGED;
    aePrecaptureFrames_ = 0;
  }

  entry = settings.find(ANDROID_CONTROL_AWB_MODE);
  const uint8_t awbMode =
      entry.count ? entry.data.u8[0] : ANDROID_CONTROL_AWB_MODE_AUTO;
  entry = settings.find(ANDROID_CONTROL_AWB_LOCK);
  const bool awbLocked =
      entry.count && entry.data.u8[0] == ANDROID_CONTROL_AWB_LOCK_ON;
  if (awbMode == ANDROID_CONTROL_AWB_MODE_OFF) {
    awbState_ = ANDROID_CONTROL_AWB_STATE_INACTIVE;
  } else if (awbLocked) {
    awbState_ = ANDROID_CONTROL_AWB_STATE_LOCKED;
  } else if (awbState_ == ANDROID_CONTROL_AWB_STATE_INACTIVE ||
             awbState_ == ANDROID_CONTROL_AWB_STATE_LOCKED) {
    awbState_ = ANDROID_CONTROL_AWB_STATE_SEARCHING;
    awbSearchFrames_ = 1;
  }
}

void ThreeA::setEmbeddedAfStatus(int status) { embeddedAfStatus_ = status; }

uint8_t ThreeA::lensState() const {
  return afState_ == ANDROID_CONTROL_AF_STATE_ACTIVE_SCAN ||
                 afState_ == ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN
             ? ANDROID_LENS_STATE_MOVING
             : ANDROID_LENS_STATE_STATIONARY;
}

void ThreeA::advanceAf(SensorController &sensor) {
  if (fixedFocus_ || afMode_ == ANDROID_CONTROL_AF_MODE_OFF) {
    afState_ = ANDROID_CONTROL_AF_STATE_INACTIVE;
    return;
  }

  if (afState_ != ANDROID_CONTROL_AF_STATE_ACTIVE_SCAN &&
      afState_ != ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN) {
    return;
  }

  const int status =
      embeddedAfStatus_ >= 0 ? embeddedAfStatus_ : sensor.afStatus();
  embeddedAfStatus_ = -1;
  ++afScanFrames_;
  const bool continuous =
      afMode_ == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE ||
      afMode_ == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
  if (status == CAMERA_AF_STATUS_SUCCESS ||
      status == CAMERA_AF_STATUS_1ST_SUCCESS) {
    afState_ = continuous ? ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED
                          : ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
    afScanFrames_ = 0;
  } else if (status == CAMERA_AF_STATUS_FAIL) {
    afState_ = continuous ? ANDROID_CONTROL_AF_STATE_PASSIVE_UNFOCUSED
                          : ANDROID_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED;
    afScanFrames_ = 0;
  } else if (status == CAMERA_AF_STATUS_RESTART && continuous) {
    afState_ = ANDROID_CONTROL_AF_STATE_PASSIVE_SCAN;
    afScanFrames_ = 0;
  } else if (afScanFrames_ >= 30) {
    // Bound drivers that leave AF stuck in progress.
    afState_ = continuous ? ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED
                          : ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
    afScanFrames_ = 0;
  }
}

void ThreeA::updateResult(CameraMetadata &result, SensorController &sensor) {
  advanceAf(sensor);
  if (aePrecaptureFrames_ && --aePrecaptureFrames_ == 0) {
    aeState_ = ANDROID_CONTROL_AE_STATE_CONVERGED;
  }
  if (awbSearchFrames_ && --awbSearchFrames_ == 0) {
    awbState_ = ANDROID_CONTROL_AWB_STATE_CONVERGED;
  }
  result.update(ANDROID_CONTROL_AF_STATE, &afState_, 1);
  result.update(ANDROID_CONTROL_AE_STATE, &aeState_, 1);
  result.update(ANDROID_CONTROL_AWB_STATE, &awbState_, 1);
}

} // namespace smdk4x12
