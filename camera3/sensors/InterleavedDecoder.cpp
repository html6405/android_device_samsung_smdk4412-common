#define LOG_TAG "Smdk4x12Interleaved"

#include "InterleavedDecoder.h"

#include <log/log.h>

#include <cerrno>
#include <cstring>
#include <limits>

namespace smdk4x12 {
namespace {

constexpr size_t kMetadataSize = 0x1000;
constexpr size_t kDecodedOffset = 4046;
constexpr size_t kAfOffset = 50;
constexpr size_t kPointerInfoOffset = 4084;

uint32_t readBigEndian32(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

} // namespace

int decodeS5c73m3Interleaved(const uint8_t *data, size_t size, uint32_t width,
                             uint32_t height, InterleavedResult *result) {
  if (!data || !result || !width || !height || size < kMetadataSize)
    return -EINVAL;
  result->decoded = false;
  result->autofocusStatus = 0;
  result->yuv.clear();
  result->jpeg.clear();

  const size_t metadata = size - kMetadataSize;
  if (metadata + kDecodedOffset >= size || metadata + kAfOffset >= size ||
      metadata + kPointerInfoOffset + 8 > size) {
    return -EIO;
  }

  result->decoded = data[metadata + kDecodedOffset] != 0;
  result->autofocusStatus = data[metadata + kAfOffset];
  if (!result->decoded)
    return 0;

  const uint32_t interleavedSize =
      readBigEndian32(data + metadata + kPointerInfoOffset);
  const uint32_t pointerBytes =
      readBigEndian32(data + metadata + kPointerInfoOffset + 4);
  const size_t lineBytes = static_cast<size_t>(width) * 2;
  const size_t expectedPointerBytes =
      static_cast<size_t>(height) * sizeof(uint32_t);
  const size_t expectedYuvBytes = lineBytes * height;

  if (lineBytes / 2 != width || expectedYuvBytes / lineBytes != height ||
      interleavedSize > metadata || pointerBytes < expectedPointerBytes ||
      pointerBytes % sizeof(uint32_t) != 0 ||
      static_cast<size_t>(interleavedSize) + pointerBytes > metadata) {
    ALOGE("invalid hybrid frame: data=%zu image=%u pointers=%u", size,
          interleavedSize, pointerBytes);
    return -EIO;
  }

  result->yuv.reserve(expectedYuvBytes);
  result->jpeg.reserve(interleavedSize > expectedYuvBytes
                           ? interleavedSize - expectedYuvBytes
                           : 0);

  const uint8_t *pointers = data + interleavedSize;
  uint32_t previousEnd = 0;
  for (uint32_t row = 0; row < height; ++row) {
    const uint32_t offset = readBigEndian32(pointers + row * sizeof(uint32_t));
    if (offset > interleavedSize || lineBytes > interleavedSize - offset ||
        offset < previousEnd) {
      result->yuv.clear();
      result->jpeg.clear();
      return -EIO;
    }

    if (offset > previousEnd) {
      result->jpeg.insert(result->jpeg.end(), data + previousEnd,
                          data + offset);
    }
    result->yuv.insert(result->yuv.end(), data + offset,
                       data + offset + lineBytes);
    previousEnd = offset + lineBytes;
  }

  if (previousEnd < interleavedSize) {
    result->jpeg.insert(result->jpeg.end(), data + previousEnd,
                        data + interleavedSize);
  }

  if (result->yuv.size() != expectedYuvBytes)
    return -EIO;
  if (!result->jpeg.empty() &&
      (result->jpeg.size() < 2 || result->jpeg[0] != 0xff ||
       result->jpeg[1] != 0xd8)) {
    ALOGW("hybrid frame contained no valid embedded JPEG");
    result->jpeg.clear();
  }
  return 0;
}

} // namespace smdk4x12
