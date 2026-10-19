#define LOG_TAG "Smdk4x12Jpeg"

#include "JpegEncoder.h"
#include "jpeg_hal.h"
#include "videodev2_exynos_media.h"

#include <algorithm>
#include <cerrno>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include <log/log.h>

extern "C" {
#include <jpeglib.h>
}

namespace smdk4x12 {
namespace {

struct SoftwareJpegError {
  jpeg_error_mgr base;
  jmp_buf jump;
};

void softwareJpegErrorExit(j_common_ptr common) {
  auto *error = reinterpret_cast<SoftwareJpegError *>(common->err);
  longjmp(error->jump, 1);
}

uint8_t clampColor(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

bool sampleYuv(const uint8_t *src, size_t srcSize, uint32_t width,
               uint32_t height, uint32_t format, uint32_t x, uint32_t y,
               uint8_t *luma, uint8_t *cb, uint8_t *cr) {
  if (!src || !luma || !cb || !cr || x >= width || y >= height)
    return false;
  if (format == V4L2_PIX_FMT_UYVY || format == V4L2_PIX_FMT_YUYV) {
    const size_t offset = (static_cast<size_t>(y) * width + (x & ~1U)) * 2;
    if (offset + 3 >= srcSize)
      return false;
    if (format == V4L2_PIX_FMT_UYVY) {
      *cb = src[offset];
      *luma = src[offset + ((x & 1U) ? 3 : 1)];
      *cr = src[offset + 2];
    } else {
      *luma = src[offset + ((x & 1U) ? 2 : 0)];
      *cb = src[offset + 1];
      *cr = src[offset + 3];
    }
    return true;
  }
  if (format == V4L2_PIX_FMT_NV12 || format == V4L2_PIX_FMT_NV21) {
    const size_t yOffset = static_cast<size_t>(y) * width + x;
    const size_t cOffset = static_cast<size_t>(width) * height +
                           static_cast<size_t>(y / 2) * width + (x & ~1U);
    if (yOffset >= srcSize || cOffset + 1 >= srcSize)
      return false;
    *luma = src[yOffset];
    if (format == V4L2_PIX_FMT_NV12) {
      *cb = src[cOffset];
      *cr = src[cOffset + 1];
    } else {
      *cr = src[cOffset];
      *cb = src[cOffset + 1];
    }
    return true;
  }
  return false;
}

bool encodeJpegSoftware(const uint8_t *src, size_t srcSize, uint32_t width,
                        uint32_t height, uint32_t inputFormat,
                        uint32_t cropLeft, uint32_t cropTop, uint32_t cropWidth,
                        uint32_t cropHeight, int quality,
                        std::vector<uint8_t> *out) {
  if (!src || !srcSize || !width || !height || !out)
    return false;
  if (!cropWidth || !cropHeight || cropLeft >= width || cropTop >= height ||
      cropWidth > width - cropLeft || cropHeight > height - cropTop)
    return false;
  jpeg_compress_struct compressor{};
  SoftwareJpegError error{};
  unsigned char *encoded = nullptr;
  unsigned char *row = nullptr;
  unsigned long encodedSize = 0;

  compressor.err = jpeg_std_error(&error.base);
  error.base.error_exit = softwareJpegErrorExit;
  if (setjmp(error.jump)) {
    jpeg_destroy_compress(&compressor);
    free(row);
    free(encoded);
    ALOGE("software JPEG compression failed");
    return false;
  }

  jpeg_create_compress(&compressor);
  jpeg_mem_dest(&compressor, &encoded, &encodedSize);
  compressor.image_width = width;
  compressor.image_height = height;
  compressor.input_components = 3;
  compressor.in_color_space = JCS_RGB;
  jpeg_set_defaults(&compressor);
  jpeg_set_quality(&compressor, std::clamp(quality, 1, 100), TRUE);
  jpeg_start_compress(&compressor, TRUE);

  row = static_cast<unsigned char *>(malloc(static_cast<size_t>(width) * 3));
  if (!row) {
    jpeg_abort_compress(&compressor);
    jpeg_destroy_compress(&compressor);
    free(encoded);
    return false;
  }
  while (compressor.next_scanline < compressor.image_height) {
    const uint32_t outputY = compressor.next_scanline;
    const uint32_t sourceY = std::min<uint32_t>(
        cropTop + cropHeight - 1,
        cropTop + static_cast<uint64_t>(outputY) * cropHeight / height);
    for (uint32_t x = 0; x < width; ++x) {
      const uint32_t sourceX = std::min<uint32_t>(
          cropLeft + cropWidth - 1,
          cropLeft + static_cast<uint64_t>(x) * cropWidth / width);
      uint8_t luma;
      uint8_t cb;
      uint8_t cr;
      if (!sampleYuv(src, srcSize, width, height, inputFormat, sourceX, sourceY,
                     &luma, &cb, &cr)) {
        jpeg_abort_compress(&compressor);
        jpeg_destroy_compress(&compressor);
        free(row);
        free(encoded);
        return false;
      }
      const int c = std::max(0, static_cast<int>(luma) - 16);
      const int d = static_cast<int>(cb) - 128;
      const int e = static_cast<int>(cr) - 128;
      row[static_cast<size_t>(x) * 3] =
          clampColor((298 * c + 409 * e + 128) >> 8);
      row[static_cast<size_t>(x) * 3 + 1] =
          clampColor((298 * c - 100 * d - 208 * e + 128) >> 8);
      row[static_cast<size_t>(x) * 3 + 2] =
          clampColor((298 * c + 516 * d + 128) >> 8);
    }
    JSAMPROW scanline = row;
    if (jpeg_write_scanlines(&compressor, &scanline, 1) != 1) {
      jpeg_abort_compress(&compressor);
      jpeg_destroy_compress(&compressor);
      free(row);
      free(encoded);
      return false;
    }
  }

  jpeg_finish_compress(&compressor);
  out->assign(encoded, encoded + encodedSize);
  jpeg_destroy_compress(&compressor);
  free(row);
  free(encoded);
  return !out->empty();
}

} // namespace

bool addExifOrientation(int degrees, std::vector<uint8_t> *jpeg) {
  if (!jpeg || jpeg->size() < 2 || (*jpeg)[0] != 0xff || (*jpeg)[1] != 0xd8) {
    return false;
  }

  uint16_t orientation;
  switch (((degrees % 360) + 360) % 360) {
  case 0:
    orientation = 1;
    break;
  case 90:
    orientation = 6;
    break;
  case 180:
    orientation = 3;
    break;
  case 270:
    orientation = 8;
    break;
  default:
    return false;
  }

  // Minimal EXIF orientation IFD.
  std::vector<uint8_t> app1 = {
      0xff, 0xe1, 0x00, 0x22, 'E',  'x',
      'i',  'f',  0x00, 0x00, 'M',  'M',
      0x00, 0x2a, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x01, 0x01, 0x12, 0x00, 0x03,
      0x00, 0x00, 0x00, 0x01, 0x00, static_cast<uint8_t>(orientation),
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  jpeg->insert(jpeg->begin() + 2, app1.begin(), app1.end());
  return true;
}

static jpeg_quality_level qualityLevel(int quality) {
  if (quality >= 90)
    return QUALITY_LEVEL_1;
  if (quality >= 80)
    return QUALITY_LEVEL_2;
  if (quality >= 70)
    return QUALITY_LEVEL_3;
  return QUALITY_LEVEL_4;
}

bool encodeJpegHardware(uint32_t sourcePhysical, size_t sourceSize,
                        uint32_t width, uint32_t height, uint32_t inputFormat,
                        int quality, uint32_t destinationPhysical,
                        size_t destinationCapacity, size_t *encodedSize) {
  if (!sourcePhysical || !destinationPhysical || !sourceSize || !width ||
      !height || !encodedSize ||
      sourceSize > static_cast<size_t>(std::numeric_limits<int>::max()) ||
      destinationCapacity >
          static_cast<size_t>(std::numeric_limits<int>::max()))
    return false;

  *encodedSize = 0;

  int fd = jpeghal_enc_init();
  if (fd < 0) {
    ALOGE("jpeghal_enc_init failed");
    return false;
  }

  jpeg_config config{};
  config.mode = JPEG_ENCODE;
  config.width = static_cast<int>(width);
  config.height = static_cast<int>(height);
  const bool semiplanar = inputFormat == V4L2_PIX_FMT_NV12 ||
                          inputFormat == V4L2_PIX_FMT_NV21;
  const size_t lumaSize = static_cast<size_t>(width) * height;
  if (semiplanar && (width & 1U || height & 1U ||
                     lumaSize + lumaSize / 2 > sourceSize)) {
    close(fd);
    return false;
  }
  config.num_planes = semiplanar ? 2 : 1;
  config.pix.enc_fmt.in_fmt = static_cast<int>(inputFormat);
  switch (inputFormat) {
  case V4L2_PIX_FMT_YUYV:
  case V4L2_PIX_FMT_UYVY:
    config.pix.enc_fmt.out_fmt = V4L2_PIX_FMT_JPEG_422;
    break;
  default:
    config.pix.enc_fmt.out_fmt = V4L2_PIX_FMT_JPEG_420;
    break;
  }
  config.enc_qual = qualityLevel(std::clamp(quality, 1, 100));

  if (jpeghal_enc_setconfig(fd, &config) < 0) {
    ALOGE("jpeghal_enc_setconfig failed: %ux%u fmt=0x%x", width, height,
          inputFormat);
    close(fd);
    return false;
  }

  // Optional on older kernels.
  (void)jpeghal_s_ctrl(fd, V4L2_CID_CACHEABLE, 1);

  // This kernel's vb2-cma-phys USERPTR ABI takes DMA physical addresses.
  // The capture frame and CAMERA_OUTPUT BLOB are already contiguous; MMAP
  // would request another CMA allocation and fails under camera load.
  jpeg_buf in{};
  in.memory = V4L2_MEMORY_USERPTR;
  in.num_planes = config.num_planes;
  in.start[0] = reinterpret_cast<void *>(
      static_cast<uintptr_t>(sourcePhysical));
  in.length[0] = static_cast<int>(semiplanar ? lumaSize : sourceSize);
  if (semiplanar) {
    in.start[1] = reinterpret_cast<void *>(
        static_cast<uintptr_t>(sourcePhysical) + lumaSize);
    in.length[1] = static_cast<int>(lumaSize / 2);
  }

  if (jpeghal_set_inbuf(fd, &in) < 0) {
    ALOGE("jpeghal_set_inbuf failed");
    close(fd);
    return false;
  }

  jpeg_buf outBuf{};
  outBuf.memory = V4L2_MEMORY_USERPTR;
  outBuf.num_planes = 1;
  outBuf.start[0] = reinterpret_cast<void *>(
      static_cast<uintptr_t>(destinationPhysical));
  outBuf.length[0] = static_cast<int>(destinationCapacity);

  if (jpeghal_set_outbuf(fd, &outBuf) < 0) {
    ALOGE("jpeghal_set_outbuf failed");
    close(fd);
    return false;
  }

  if (jpeghal_enc_exe(fd, &in, &outBuf) < 0) {
    ALOGE("jpeghal_enc_exe failed");
    jpeghal_deinit(fd, &in, &outBuf);
    return false;
  }

  const int jpegBytes = jpeghal_g_ctrl(fd, V4L2_CID_CAM_JPEG_ENCODEDSIZE);
  if (jpegBytes <= 0 || jpegBytes > outBuf.length[0]) {
    ALOGE("invalid encoded JPEG size %d (buffer=%d)", jpegBytes,
          outBuf.length[0]);
    jpeghal_deinit(fd, &in, &outBuf);
    return false;
  }

  *encodedSize = static_cast<size_t>(jpegBytes);
  jpeghal_deinit(fd, &in, &outBuf);
  return true;
}

bool encodeJpegCropped(const uint8_t *src, size_t srcSize, uint32_t sourceWidth,
                       uint32_t sourceHeight, uint32_t inputFormat,
                       uint32_t cropLeft, uint32_t cropTop, uint32_t cropWidth,
                       uint32_t cropHeight, int quality,
                       std::vector<uint8_t> *out) {
  return encodeJpegSoftware(src, srcSize, sourceWidth, sourceHeight,
                            inputFormat, cropLeft, cropTop, cropWidth,
                            cropHeight, quality, out);
}

} // namespace smdk4x12
