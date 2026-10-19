// HAL3-only V4L2 helpers.
#include <cerrno>
#include <sys/ioctl.h>
namespace smdk4x12 {
int xioctl(int fd, unsigned long req, void *arg) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r < 0 && errno == EINTR);
  return r;
}
} // namespace smdk4x12
