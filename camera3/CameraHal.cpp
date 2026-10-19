#define LOG_TAG "camera.smdk4x12"

#include "SmdkCamera.h"

#include <hardware/hardware.h>
#include <log/log.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

namespace {

constexpr int kCameraCount = 2;
std::mutex gModuleLock;
const camera_module_callbacks_t *gCallbacks = nullptr;
std::unique_ptr<smdk4x12::ExynosPipeline> gTorchPipeline;
std::unique_ptr<smdk4x12::SensorController> gTorchSensor;
int gTorchCameraId = -1;
std::array<bool, kCameraCount> gCameraOpen{};

bool anyCameraOpenLocked() {
  return std::any_of(gCameraOpen.begin(), gCameraOpen.end(),
                     [](bool open) { return open; });
}

void disableTorchLocked() {
  if (gTorchSensor)
    (void)gTorchSensor->setTorch(false);
  if (gTorchPipeline)
    gTorchPipeline->close();
  gTorchSensor.reset();
  gTorchPipeline.reset();
  gTorchCameraId = -1;
}

int parseCameraId(const char *text) {
  if (!text || !*text)
    return -EINVAL;
  char *end = nullptr;
  errno = 0;
  const long id = strtol(text, &end, 10);
  if (errno || !end || *end != '\0' || id < 0 || id >= kCameraCount)
    return -EINVAL;
  return static_cast<int>(id);
}

int getNumberOfCameras() { return kCameraCount; }

int getCameraInfo(int id, camera_info *info) {
  if (!info || id < 0 || id >= kCameraCount)
    return -EINVAL;
  memset(info, 0, sizeof(*info));
  info->facing = smdk4x12::facing(id);
  info->orientation = smdk4x12::orientation(id);
  info->device_version = CAMERA_DEVICE_API_VERSION_3_5;
  info->static_camera_characteristics = smdk4x12::staticMetadata(id);
  info->resource_cost = 100;
  static char camera0[] = "0";
  static char camera1[] = "1";
  static char *camera0Conflicts[] = {camera1};
  static char *camera1Conflicts[] = {camera0};
  info->conflicting_devices = id == 0 ? camera0Conflicts : camera1Conflicts;
  info->conflicting_devices_length = 1;
  return info->static_camera_characteristics ? 0 : -ENODEV;
}

int openCamera(const hw_module_t *module, const char *name,
               hw_device_t **output) {
  if (!module || !output)
    return -EINVAL;
  const int id = parseCameraId(name);
  if (id < 0)
    return id;

  const camera_module_callbacks_t *callbacks = nullptr;
  {
    std::lock_guard<std::mutex> guard(gModuleLock);
    if (anyCameraOpenLocked())
      return -EBUSY;
    if (gTorchCameraId >= 0)
      disableTorchLocked();
    gCameraOpen[id] = true;
    callbacks = gCallbacks;
  }
  if (callbacks && callbacks->torch_mode_status_change &&
      smdk4x12::sensorProfile(0).flash) {
    callbacks->torch_mode_status_change(callbacks, "0",
                                        TORCH_MODE_STATUS_NOT_AVAILABLE);
  }

  auto camera = std::make_unique<smdk4x12::CameraDevice>(id, [id] {
    const camera_module_callbacks_t *callbacks = nullptr;
    {
      std::lock_guard<std::mutex> guard(gModuleLock);
      gCameraOpen[id] = false;
      callbacks = gCallbacks;
    }
    if (callbacks && callbacks->torch_mode_status_change &&
        smdk4x12::sensorProfile(0).flash) {
      callbacks->torch_mode_status_change(callbacks, "0",
                                          TORCH_MODE_STATUS_AVAILABLE_OFF);
    }
  });
  camera3_device_t *device = nullptr;
  const int status = camera->open(module, &device);
  if (status)
    return status;
  *output = &device->common;
  camera.release();
  return 0;
}

int setCallbacks(const camera_module_callbacks_t *callbacks) {
  if (!callbacks)
    return -EINVAL;
  std::lock_guard<std::mutex> guard(gModuleLock);
  gCallbacks = callbacks;
  return 0;
}

int setTorchMode(const char *cameraId, bool enabled) {
  const int id = parseCameraId(cameraId);
  if (id < 0)
    return id;
  if (!smdk4x12::sensorProfile(id).flash)
    return -ENOSYS;

  std::lock_guard<std::mutex> guard(gModuleLock);
  if (anyCameraOpenLocked())
    return -EBUSY;
  int status = 0;
  if (!enabled) {
    if (gTorchCameraId == id)
      disableTorchLocked();
  } else {
    disableTorchLocked();
    auto pipeline = std::make_unique<smdk4x12::ExynosPipeline>();
    status = pipeline->open(id, smdk4x12::sensorProfile(id).fimcIs);
    if (!status) {
      auto sensor =
          std::make_unique<smdk4x12::SensorController>(id, pipeline.get());
      status = sensor->setTorch(true);
      if (!status) {
        gTorchCameraId = id;
        gTorchPipeline = std::move(pipeline);
        gTorchSensor = std::move(sensor);
      }
    }
  }
  if (!status && gCallbacks && gCallbacks->torch_mode_status_change) {
    gCallbacks->torch_mode_status_change(gCallbacks, cameraId,
                                         enabled
                                             ? TORCH_MODE_STATUS_AVAILABLE_ON
                                             : TORCH_MODE_STATUS_AVAILABLE_OFF);
  }
  return status;
}

int initializeModule() {
  // Validate metadata before publishing cameras.
  for (int id = 0; id < kCameraCount; ++id) {
    if (!smdk4x12::staticMetadata(id))
      return -ENODEV;
  }
  return 0;
}

hw_module_methods_t gModuleMethods{
    .open = openCamera,
};

} // namespace

extern "C" __attribute__((
    visibility("default"))) camera_module_t HAL_MODULE_INFO_SYM = {
    .common =
        {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = CAMERA_HARDWARE_MODULE_ID,
            .name = "Native Exynos4 smdk4x12 Camera HAL3",
            .author = "LineageOS smdk4412 maintainers",
            .methods = &gModuleMethods,
            .dso = nullptr,
            .reserved = {},
        },
    .get_number_of_cameras = getNumberOfCameras,
    .get_camera_info = getCameraInfo,
    .set_callbacks = setCallbacks,
    .get_vendor_tag_ops = nullptr,
    .open_legacy = nullptr,
    .set_torch_mode = setTorchMode,
    .init = initializeModule,
    .get_physical_camera_info = nullptr,
    .is_stream_combination_supported = nullptr,
    .notify_device_state_change = nullptr,
    .set_torch_mode_strength = nullptr,
    .reserved = {},
};
