#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace smdk4x12 {

struct InterleavedResult {
  bool decoded = false;
  int autofocusStatus = 0;
  std::vector<uint8_t> yuv;
  std::vector<uint8_t> jpeg;
};

int decodeS5c73m3Interleaved(const uint8_t *data, size_t size, uint32_t width,
                             uint32_t height, InterleavedResult *result);

} // namespace smdk4x12
