#include "CameraMetadata.h"

#include <algorithm>
#include <cerrno>
#include <utility>

namespace smdk4x12 {
namespace {

constexpr size_t kInitialEntryCapacity = 256;
constexpr size_t kInitialDataCapacity = 32768;

} // namespace

CameraMetadata::CameraMetadata() {
  buffer_ =
      allocate_camera_metadata(kInitialEntryCapacity, kInitialDataCapacity);
}

CameraMetadata::CameraMetadata(const camera_metadata_t *source) {
  assign(source);
}

CameraMetadata::CameraMetadata(const CameraMetadata &other) {
  assign(other.buffer_);
}

CameraMetadata::CameraMetadata(CameraMetadata &&other) noexcept
    : buffer_(std::exchange(other.buffer_, nullptr)) {}

CameraMetadata::~CameraMetadata() {
  if (buffer_)
    free_camera_metadata(buffer_);
}

CameraMetadata &CameraMetadata::operator=(const CameraMetadata &other) {
  if (this != &other)
    assign(other.buffer_);
  return *this;
}

CameraMetadata &CameraMetadata::operator=(CameraMetadata &&other) noexcept {
  if (this == &other)
    return *this;
  if (buffer_)
    free_camera_metadata(buffer_);
  buffer_ = std::exchange(other.buffer_, nullptr);
  return *this;
}

CameraMetadata &CameraMetadata::operator=(const camera_metadata_t *source) {
  assign(source);
  return *this;
}

int CameraMetadata::assign(const camera_metadata_t *source) {
  const size_t entries = source ? get_camera_metadata_entry_count(source) : 0;
  const size_t data = source ? get_camera_metadata_data_count(source) : 0;
  camera_metadata_t *replacement =
      allocate_camera_metadata(std::max(kInitialEntryCapacity, entries + 32),
                               std::max(kInitialDataCapacity, data + 4096));
  if (!replacement)
    return -ENOMEM;
  if (source && append_camera_metadata(replacement, source)) {
    free_camera_metadata(replacement);
    return -EINVAL;
  }
  if (buffer_)
    free_camera_metadata(buffer_);
  buffer_ = replacement;
  return 0;
}

int CameraMetadata::ensureCapacity(size_t extraEntries, size_t extraData) {
  if (!buffer_)
    return assign(nullptr);
  const size_t entries = get_camera_metadata_entry_count(buffer_);
  const size_t data = get_camera_metadata_data_count(buffer_);
  if (entries + extraEntries <= get_camera_metadata_entry_capacity(buffer_) &&
      data + extraData <= get_camera_metadata_data_capacity(buffer_)) {
    return 0;
  }

  camera_metadata_t *replacement = allocate_camera_metadata(
      std::max(get_camera_metadata_entry_capacity(buffer_) * 2,
               entries + extraEntries + 32),
      std::max(get_camera_metadata_data_capacity(buffer_) * 2,
               data + extraData + 4096));
  if (!replacement)
    return -ENOMEM;
  if (append_camera_metadata(replacement, buffer_)) {
    free_camera_metadata(replacement);
    return -EINVAL;
  }
  free_camera_metadata(buffer_);
  buffer_ = replacement;
  return 0;
}

int CameraMetadata::updateImpl(uint32_t tag, uint8_t expectedType,
                               const void *data, size_t count) {
  if (!data || get_camera_metadata_tag_type(tag) != expectedType)
    return -EINVAL;
  const size_t dataSize =
      calculate_camera_metadata_entry_data_size(expectedType, count);
  int status = ensureCapacity(1, dataSize);
  if (status)
    return status;

  camera_metadata_entry_t entry{};
  status = find_camera_metadata_entry(buffer_, tag, &entry);
  if (!status)
    return update_camera_metadata_entry(buffer_, entry.index, data, count,
                                        nullptr);
  return add_camera_metadata_entry(buffer_, tag, data, count);
}

int CameraMetadata::update(uint32_t tag, const uint8_t *data, size_t count) {
  return updateImpl(tag, TYPE_BYTE, data, count);
}

int CameraMetadata::update(uint32_t tag, const int32_t *data, size_t count) {
  return updateImpl(tag, TYPE_INT32, data, count);
}

int CameraMetadata::update(uint32_t tag, const float *data, size_t count) {
  return updateImpl(tag, TYPE_FLOAT, data, count);
}

int CameraMetadata::update(uint32_t tag, const int64_t *data, size_t count) {
  return updateImpl(tag, TYPE_INT64, data, count);
}

int CameraMetadata::update(uint32_t tag, const double *data, size_t count) {
  return updateImpl(tag, TYPE_DOUBLE, data, count);
}

int CameraMetadata::update(uint32_t tag, const camera_metadata_rational_t *data,
                           size_t count) {
  return updateImpl(tag, TYPE_RATIONAL, data, count);
}

camera_metadata_ro_entry_t CameraMetadata::find(uint32_t tag) const {
  camera_metadata_ro_entry_t entry{};
  if (!buffer_ || find_camera_metadata_ro_entry(buffer_, tag, &entry)) {
    entry.count = 0;
    entry.data.u8 = nullptr;
  }
  return entry;
}

bool CameraMetadata::isEmpty() const {
  return !buffer_ || get_camera_metadata_entry_count(buffer_) == 0;
}

} // namespace smdk4x12
