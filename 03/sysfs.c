#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "file.h"

int
sysfs_pci_open_device(const char *device) {
  return open_at("/sys/bus/pci/devices", device, O_PATH);
}

int
sysfs_pci_open_config(int devfd) {
  return openat(devfd, "config", O_RDONLY);
}

ssize_t
sysfs_pci_config_read_header_type(int fd, unsigned char *pci_header_type) {
  return pread(fd, pci_header_type, 1, 0x0E);
}

int
sysfs_pci_bind_driver(int devfd, const char *device, const char *driver) {
  int result;
  result = write_file_at(devfd, "driver/unbind", device);
  if ((result < 0) && (errno != ENOENT))
    return result;

  result = write_file_at(devfd, "driver_override", driver);
  if (result < 0)
    return result;

  result = open_at("/sys/bus/pci/drivers", driver, O_PATH);
  if (result < 0)
    return result;

  int drvfd = result;
  result = write_file_at(drvfd, "bind", device);
  close(drvfd);
  return result;
}
