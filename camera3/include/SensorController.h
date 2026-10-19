#pragma once

#include "CameraMetadata.h"

#include <cstdint>
#include <span>
#include <unordered_map>

namespace smdk4x12 {

class ExynosPipeline;

enum class SensorKind { S5C73M3, ISX012, S5K6A3 };

struct Size {
  uint32_t width;
  uint32_t height;
};

struct SensorProfile {
  SensorKind kind;
  const char *name;
  bool front;
  bool fimcIs;
  bool fixedFocus;
  bool flash;
  bool aeLock;
  bool awbLock;
  int orientation;
  float focalLength;
  float horizontalViewAngle;
  float verticalViewAngle;
  float physicalWidth;
  float physicalHeight;
  uint32_t maxWidth;
  uint32_t maxHeight;
  uint32_t captureFormat;
  std::span<const Size> outputSizes;
};

const SensorProfile &sensorProfile(int cameraId);

class SensorController {
public:
  SensorController(int cameraId, ExynosPipeline *pipeline);

  int apply(const CameraMetadata &settings);
  int triggerAf(uint8_t trigger);
  int afStatus();
  int setTorch(bool enabled);
  bool shouldFireCaptureFlash(const CameraMetadata &settings);
  int setCaptureFlash(bool enabled);
  void setPreviewSize(uint32_t width, uint32_t height);
  void resetControlCache();

  const SensorProfile &profile() const { return profile_; }

private:
  int setBestEffort(uint32_t id, int value);
  int setCached(uint32_t id, int value);
  static int mapAf(uint8_t value);
  static int mapAwb(uint8_t value);
  static int mapScene(uint8_t value);
  static int mapEffect(uint8_t value);
  static int mapAntibanding(uint8_t value);

  ExynosPipeline *pipeline_;
  const SensorProfile &profile_;
  std::unordered_map<uint32_t, int> controlCache_;
  uint32_t previewWidth_ = 0;
  uint32_t previewHeight_ = 0;
  bool touchPointValid_ = false;
  uint8_t lastAndroidAfMode_ = 0xff;
};

} // namespace smdk4x12
