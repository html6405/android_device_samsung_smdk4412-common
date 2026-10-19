#pragma once

#include "CameraMetadata.h"

#include <cstdint>

namespace smdk4x12 {

class SensorController;

class ThreeA {
public:
  void reset(bool fixedFocus);
  void applyRequest(const CameraMetadata &settings, SensorController &sensor);
  void setEmbeddedAfStatus(int status);
  void updateResult(CameraMetadata &result, SensorController &sensor);
  uint8_t lensState() const;

private:
  void advanceAf(SensorController &sensor);

  uint8_t afMode_ = 0;
  uint8_t afState_ = 0;
  uint8_t aeState_ = 0;
  uint8_t awbState_ = 0;
  int embeddedAfStatus_ = -1;
  unsigned afScanFrames_ = 0;
  unsigned aePrecaptureFrames_ = 0;
  unsigned awbSearchFrames_ = 0;
  bool fixedFocus_ = false;
};

} // namespace smdk4x12
