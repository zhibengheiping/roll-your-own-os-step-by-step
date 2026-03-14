#pragma once

#include <sys/mman.h>
#include <linux/vfio.h>

struct vfio_pci_dev {
  int container;
  int fd;
  struct vfio_iommu_type1_info *iommu_info;
  struct vfio_device_info *device_info;
  struct vfio_region_info **region_info;
  struct vfio_irq_info **irq_info;
  void *bar[6];
  __u32 irq_index;
  __u32 num_irqs;
  int *eventfd;
};

int
vfio_pci_dev_open(const char *device, struct vfio_pci_dev *dev);

void
vfio_print_info(struct vfio_pci_dev *dev);

int
vfio_pci_dev_init(struct vfio_pci_dev *dev);

void *
vfio_pci_dev_map_dma(struct vfio_pci_dev *dev, size_t size, __u64 *iova);

int
vfio_pci_dev_unmap_dma(struct vfio_pci_dev *dev, size_t size, __u64 iova);

void
vfio_pci_dev_close(struct vfio_pci_dev *dev);

int
vfio_pci_dev_read_config(struct vfio_pci_dev *dev, void *buf, size_t count, off_t offset);

int
vfio_pci_dev_write_config(struct vfio_pci_dev *dev, const void *buf, size_t count, off_t offset);
