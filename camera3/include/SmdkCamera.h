#pragma once

#include "CameraMetadata.h"
#include <hardware/camera3.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ExynosPipeline.h"
#include "SensorController.h"
#include "StreamProcessor.h"
#include "ThreeA.h"

namespace smdk4x12 {

struct CaptureRequest {
  uint32_t frameNumber = 0;
  uint64_t generation = 0;
  bool flashFired = false;
  CameraMetadata settings;
  std::vector<camera3_stream_buffer_t> outputBuffers;
};

class CameraDevice {
public:
  CameraDevice(int id, std::function<void()> onClosed);
  ~CameraDevice();

  int open(const hw_module_t *module, camera3_device_t **device);
  int initialize(const camera3_callback_ops_t *callbacks);
  int configureStreams(camera3_stream_configuration_t *configuration);
  const camera_metadata_t *defaultRequest(int type);
  int processCaptureRequest(camera3_capture_request_t *request);
  int flush();
  void dump(int fd) const;
  void close();

  static CameraDevice *from(const camera3_device_t *device);

private:
  enum class State { Open, Initialized, Configured, Closed };

  void workerLoop();
  int processOne(CaptureRequest *request);
  void returnResult(CaptureRequest *request, int status, int64_t timestampNs,
                    bool shutterSent);
  void notifyShutter(uint32_t frameNumber, int64_t timestampNs);
  void notifyError(uint32_t frameNumber, camera3_error_msg_code_t code,
                   camera3_stream_t *stream = nullptr);
  int waitAcquireFence(camera3_stream_buffer_t *buffer);
  int writeJpeg(const Frame &frame, camera3_stream_buffer_t *output,
                const CameraMetadata &settings,
                const std::vector<uint8_t> &embeddedJpeg,
                const CropRegion &crop);
  void initializeDefaultRequests();

  int id_;
  std::function<void()> onClosed_;
  camera3_device_t device_{};
  camera3_device_ops_t operations_{};
  const camera3_callback_ops_t *callbacks_ = nullptr;
  State state_ = State::Open;

  std::unique_ptr<SensorController> sensor_;
  ExynosPipeline pipeline_;
  StreamProcessor processor_;
  ThreeA threeA_;

  mutable std::mutex lock_;
  std::condition_variable queueChanged_;
  std::condition_variable drained_;
  std::deque<CaptureRequest> queue_;
  std::thread worker_;
  bool exit_ = false;
  bool flushing_ = false;
  uint64_t generation_ = 1;
  size_t activeRequests_ = 0;
  uint32_t lastFrameNumber_ = 0;
  bool haveFrameNumber_ = false;

  CameraMetadata lastSettings_;
  std::array<CameraMetadata, CAMERA3_TEMPLATE_COUNT> defaultRequests_;
  std::array<const camera_metadata_t *, CAMERA3_TEMPLATE_COUNT>
      defaultRequestPointers_{};

  std::vector<camera3_stream_t *> streams_;
  uint32_t sourceWidth_ = 0;
  uint32_t sourceHeight_ = 0;
  int configuredFps_ = 30;
};

const camera_metadata_t *staticMetadata(int id);
bool isSupportedStream(int id, const camera3_stream_t &stream);
int facing(int id);
int orientation(int id);

} // namespace smdk4x12
