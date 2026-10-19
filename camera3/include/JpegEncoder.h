#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace smdk4x12 {

bool encodeJpegHardware(uint32_t sourcePhysical, size_t sourceSize,
                        uint32_t width, uint32_t height, uint32_t inputFormat,
                        int quality, uint32_t destinationPhysical,
                        size_t destinationCapacity, size_t *encodedSize);
bool encodeJpegCropped(const uint8_t *source, size_t sourceSize,
                       uint32_t sourceWidth, uint32_t sourceHeight,
                       uint32_t inputFormat, uint32_t cropLeft,
                       uint32_t cropTop, uint32_t cropWidth,
                       uint32_t cropHeight, int quality,
                       std::vector<uint8_t> *output);
bool addExifOrientation(int degrees, std::vector<uint8_t> *jpeg);

} // namespace smdk4x12
