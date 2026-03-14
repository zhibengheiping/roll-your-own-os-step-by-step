#pragma once

int
sysfs_pci_open_device(const char *device);

int
sysfs_pci_open_config(int devfd);

int
sysfs_pci_bind_driver(int devfd, const char *device, const char *driver);

ssize_t
sysfs_pci_config_read_header_type(int configfd, unsigned char *header_type);
