#include "SensorController.h"

#include <android-base/properties.h>
#include <linux/videodev2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace smdk4x12 {
namespace {

constexpr std::array kRearPhoneSizes = {
    Size{3264, 2448}, Size{3264, 2176}, Size{3264, 1836}, Size{2560, 1920},
    Size{2048, 1536}, Size{2048, 1152}, Size{1920, 1080}, Size{1600, 1200},
    Size{1280, 720},  Size{1024, 768},  Size{960, 720},   Size{720, 480},
    Size{640, 480},   Size{352, 288},   Size{320, 240},
};

constexpr std::array kRearTabletSizes = {
    Size{2560, 1920}, Size{2560, 1440}, Size{2048, 1536}, Size{2048, 1152},
    Size{1920, 1080}, Size{1600, 1200}, Size{1536, 864},  Size{1280, 1024},
    Size{1280, 720},  Size{1024, 768},  Size{960, 720},   Size{720, 480},
    Size{640, 480},   Size{352, 288},   Size{320, 240},
};

constexpr std::array kFrontSizes = {
    Size{1392, 1392}, Size{1280, 960}, Size{1280, 720}, Size{960, 720},
    Size{704, 704},   Size{640, 480},  Size{320, 320},  Size{320, 240},
};

const SensorProfile kRearPhone{
    .kind = SensorKind::S5C73M3,
    .name = "S5C73M3",
    .front = false,
    .fimcIs = false,
    .fixedFocus = false,
    .flash = true,
    .aeLock = true,
    .awbLock = true,
    .orientation = 90,
    .focalLength = 3.7f,
    .horizontalViewAngle = 63.0f,
    .verticalViewAngle = 49.3f,
    .physicalWidth = 4.8f,
    .physicalHeight = 3.6f,
    .maxWidth = 3264,
    .maxHeight = 2448,
    .captureFormat = v4l2_fourcc('I', 'T', 'L', 'V'),
    .outputSizes = kRearPhoneSizes,
};

const SensorProfile kRearTablet{
    .kind = SensorKind::ISX012,
    .name = "ISX012",
    .front = false,
    .fimcIs = false,
    .fixedFocus = false,
    .flash = true,
    .aeLock = false,
    .awbLock = false,
    .orientation = 0,
    .focalLength = 3.43f,
    .horizontalViewAngle = 59.6f,
    .verticalViewAngle = 46.3f,
    .physicalWidth = 4.54f,
    .physicalHeight = 3.42f,
    .maxWidth = 2560,
    .maxHeight = 1920,
    .captureFormat = V4L2_PIX_FMT_NV21,
    .outputSizes = kRearTabletSizes,
};

const SensorProfile kFrontPhone{
    .kind = SensorKind::S5K6A3,
    .name = "S5K6A3",
    .front = true,
    .fimcIs = true,
    .fixedFocus = true,
    .flash = false,
    .aeLock = false,
    .awbLock = false,
    .orientation = 270,
    .focalLength = 2.73f,
    .horizontalViewAngle = 65.0f,
    .verticalViewAngle = 50.0f,
    .physicalWidth = 3.2f,
    .physicalHeight = 2.4f,
    .maxWidth = 1392,
    .maxHeight = 1392,
    .captureFormat = V4L2_PIX_FMT_UYVY,
    .outputSizes = kFrontSizes,
};

const SensorProfile kFrontTablet = [] {
  SensorProfile profile = kFrontPhone;
  profile.orientation = 0;
  return profile;
}();

bool isTablet() {
  const std::array properties = {
      "ro.product.device", "ro.product.vendor.device", "ro.product.odm.device",
      "ro.product.model",  "ro.product.vendor.model",  "ro.boot.hardware",
  };
  for (const char *property : properties) {
    std::string value = android::base::GetProperty(property, "");
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (value.find("n80") != std::string::npos ||
        value.find("n51") != std::string::npos ||
        value.find("i925") != std::string::npos ||
        value.find("p4note") != std::string::npos ||
        value.find("lt01") != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

const SensorProfile &sensorProfile(int cameraId) {
  if (cameraId == 0)
    return isTablet() ? kRearTablet : kRearPhone;
  return isTablet() ? kFrontTablet : kFrontPhone;
}

} // namespace smdk4x12
