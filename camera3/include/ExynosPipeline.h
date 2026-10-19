#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace smdk4x12 {

struct Frame {
  uint8_t *data = nullptr;
  size_t size = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  uint32_t paddrY = 0;
  uint32_t paddrCbcr = 0;
  int dmabufFd = -1; // Borrowed from the capture queue.
  int index = -1;
  int64_t timestampNs = 0;
};

class ExynosPipeline {
public:
  ExynosPipeline();
  ~ExynosPipeline();

  int open(int cameraId, bool fimcIs);
  int configure(uint32_t width, uint32_t height, uint32_t format, int fps);
  int start();
  int dequeue(Frame *frame);
  int requeue(const Frame &frame);
  int stop();
  void close();

  void requestAbort();
  void clearAbort();

  int setControl(uint32_t id, int value);
  int getControl(uint32_t id, int *value);
  bool dmabufSupported() const;

private:
  struct Buffer {
    void *ptr = nullptr;
    size_t length = 0;
    uint32_t paddr = 0;
    int dmabufFd = -1;
  };

  void releaseBuffersLocked();

  mutable std::mutex lock_;
  int fd_ = -1;
  int abortFd_ = -1;
  int cameraId_ = -1;
  bool fimcIs_ = false;
  bool streaming_ = false;
  bool dmabufSupported_ = false;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t format_ = 0;
  std::vector<Buffer> buffers_;
};

} // namespace smdk4x12
