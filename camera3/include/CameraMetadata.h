#pragma once

#include <system/camera_metadata.h>

#include <cstddef>
#include <cstdint>

namespace smdk4x12 {

// Vendor-safe camera_metadata_t owner.
class CameraMetadata {
public:
  CameraMetadata();
  explicit CameraMetadata(const camera_metadata_t *source);
  CameraMetadata(const CameraMetadata &other);
  CameraMetadata(CameraMetadata &&other) noexcept;
  ~CameraMetadata();

  CameraMetadata &operator=(const CameraMetadata &other);
  CameraMetadata &operator=(CameraMetadata &&other) noexcept;
  CameraMetadata &operator=(const camera_metadata_t *source);

  int update(uint32_t tag, const uint8_t *data, size_t count);
  int update(uint32_t tag, const int32_t *data, size_t count);
  int update(uint32_t tag, const float *data, size_t count);
  int update(uint32_t tag, const int64_t *data, size_t count);
  int update(uint32_t tag, const double *data, size_t count);
  int update(uint32_t tag, const camera_metadata_rational_t *data,
             size_t count);

  camera_metadata_ro_entry_t find(uint32_t tag) const;
  bool isEmpty() const;
  const camera_metadata_t *getAndLock() const { return buffer_; }
  int unlock(const camera_metadata_t *) const { return 0; }

private:
  int assign(const camera_metadata_t *source);
  int ensureCapacity(size_t extraEntries, size_t extraData);
  int updateImpl(uint32_t tag, uint8_t expectedType, const void *data,
                 size_t count);

  camera_metadata_t *buffer_ = nullptr;
};

} // namespace smdk4x12
