#pragma once

#include <hardware/camera3.h>

#include <memory>
#include <mutex>
#include <vector>

#include "ExynosPipeline.h"

class SecFimc;

namespace smdk4x12 {

struct CropRegion {
  uint32_t left = 0;
  uint32_t top = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

// Parallel FIMC output lanes with CPU fallback.
class StreamProcessor {
public:
  StreamProcessor();
  ~StreamProcessor();

  int write(const Frame &frame, camera3_stream_buffer_t *output,
            const CropRegion &crop);
  std::vector<int>
  writeMany(const Frame &frame,
            const std::vector<camera3_stream_buffer_t *> &outputs,
            const CropRegion &crop);
  void reset();

private:
  struct Lane;

  int writeOnLane(const Frame &frame, camera3_stream_buffer_t *output,
                  const CropRegion &crop, Lane *lane);
  int writeWithFimc(const Frame &frame, camera3_stream_buffer_t *output,
                    const CropRegion &crop, Lane *lane);
  int writeWithCpu(const Frame &frame, camera3_stream_buffer_t *output,
                   const CropRegion &crop);

  std::mutex lanesLock_;
  std::unique_ptr<Lane> previewLane_;
  std::unique_ptr<Lane> videoLane_;
};

} // namespace smdk4x12
