#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/pci.h>

#include "file.h"
#include "sysfs.h"
#include "vfio.h"

static
void
print_flags(__u32 flags, __u32 n, const char *names[n]) {
  for(__u32 i=0; i<n; i++)
    if (names[i])
      if (flags & (1 << i))
        printf(" %s", names[i]);
}

static
struct vfio_info_cap_header *
vfio_find_cap(void *addr, __u32 offset) {
  if (!offset)
    return NULL;
  return (struct vfio_info_cap_header *)(addr+offset);
}

static
int
vfio_get_container_fd() {
  int fd = open("/dev/vfio/vfio", O_RDWR);
  if (fd < 0)
    return fd;
  int result = -1;
  if (ioctl(fd, VFIO_GET_API_VERSION) != VFIO_API_VERSION)
    goto err;
  return fd;
 err:
  close(fd);
  return result;
}

static
int
vfio_get_group_fd(int container, const char *number) {
  int fd = open_at("/dev/vfio", number, O_RDWR);
  if (fd < 0)
    return fd;

  struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
  int result = ioctl(fd, VFIO_GROUP_GET_STATUS, &group_status);
  if (result < 0)
    goto err;

  result = -1;

  if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE))
    goto err;

  result = ioctl(fd, VFIO_GROUP_SET_CONTAINER, &container);
  if (result < 0)
    goto err;

  return fd;
 err:
  close(fd);
  return result;
}

static
int
vfio_get_iommu_info(int fd, struct vfio_iommu_type1_info **iommu_info) {
  *iommu_info = (struct vfio_iommu_type1_info *)malloc(sizeof(struct vfio_iommu_type1_info));
  (*iommu_info)->argsz = sizeof(struct vfio_iommu_type1_info);
  int result = ioctl(fd, VFIO_IOMMU_GET_INFO, *iommu_info);
  if (result < 0)
    goto err;

  if ((*iommu_info)->argsz > sizeof(struct vfio_iommu_type1_info)) {
    *iommu_info = (struct vfio_iommu_type1_info *)realloc(*iommu_info, (*iommu_info)->argsz);
    result = ioctl(fd, VFIO_IOMMU_GET_INFO, *iommu_info);
    if (result < 0)
      goto err;
  }
  return result;
 err:
  free(*iommu_info);
  *iommu_info = NULL;
  return result;
}

static
void
vfio_print_iommu_info(struct vfio_iommu_type1_info *iommu_info) {
  static char const* NAMES[] = {"pgsizes", "caps"};
  printf("IOMMU INFO\n  flags:");
  print_flags(iommu_info->flags, 2, NAMES);
  printf("\n");

  struct vfio_info_cap_header *header;
  for(__u32 offset=iommu_info->cap_offset;
      (header = vfio_find_cap(iommu_info, offset));
      offset = header->next)
    if (header->version == 1)
      switch (header->id) {
      case VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE:
        {
          struct vfio_iommu_type1_info_cap_iova_range *cap = (struct vfio_iommu_type1_info_cap_iova_range *)header;
          for(__u32 i=0; i<cap->nr_iovas; i++)
            printf("  iova range: %016llx-%016llx\n", cap->iova_ranges[i].start, cap->iova_ranges[i].end);
          break;
        }
      case VFIO_IOMMU_TYPE1_INFO_CAP_MIGRATION:
        {
          struct vfio_iommu_type1_info_cap_migration *cap = (struct vfio_iommu_type1_info_cap_migration *)header;
          printf("  migration: flags %08x pgsize %016llx dirtybitmap %016llx\n", cap->flags, cap->pgsize_bitmap, cap->max_dirty_bitmap_size);
          break;
        }
      case VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL:
        {
          struct vfio_iommu_type1_info_dma_avail *cap = (struct vfio_iommu_type1_info_dma_avail *)header;
          printf("  avail: %u\n", cap->avail);
          break;
        }
      }
}

static
int
vfio_get_device_info(int fd, struct vfio_device_info **device_info) {
  *device_info = (struct vfio_device_info *)malloc(sizeof(struct vfio_device_info));
  (*device_info)->argsz = sizeof(struct vfio_device_info);
  int result = ioctl(fd, VFIO_DEVICE_GET_INFO, *device_info);
  if (result < 0)
    goto err;

  if((*device_info)->argsz > sizeof(struct vfio_device_info)) {
    *device_info = (struct vfio_device_info *)realloc(*device_info, (*device_info)->argsz);
    result = ioctl(fd, VFIO_DEVICE_GET_INFO, *device_info);
    if (result < 0)
      goto err;
  }
  return result;
 err:
  free(*device_info);
  *device_info = NULL;
  return result;
}

static
void
vfio_print_device_info(struct vfio_device_info *device_info) {
  static char const* NAMES[] = {
    "reset", "pci", "platform", "amba", "ccw", "ap", "fsl_mc", "caps",
  };

  printf("DEVICE INFO\n  flags:");
  print_flags(device_info->flags, 8, NAMES);
  printf("\n");
}

static
int
vfio_get_region_info(int fd, __u32 index, struct vfio_region_info **region_info) {
  *region_info = (struct vfio_region_info *)malloc(sizeof(struct vfio_region_info));
  (*region_info)->argsz = sizeof(struct vfio_region_info);
  (*region_info)->index = index;
  int result = ioctl(fd, VFIO_DEVICE_GET_REGION_INFO, *region_info);
  if (result < 0)
    goto err;

  if((*region_info)->argsz > sizeof(struct vfio_region_info)) {
    *region_info = (struct vfio_region_info *)realloc(*region_info, (*region_info)->argsz);
    result = ioctl(fd, VFIO_DEVICE_GET_REGION_INFO, *region_info);
    if (result < 0)
      goto err;
  }
  return result;
 err:
  free(*region_info);
  *region_info = NULL;
  return result;
}

static const char *region_name[] = {
  "bar0",
  "bar1",
  "bar2",
  "bar3",
  "bar4",
  "bar5",
  "rom",
  "config",
  "vga",
};

static
void
vfio_print_region_info(__u32 num_regions, struct vfio_region_info *region_info[num_regions]) {
  static char const* NAMES[] = {
    "read", "write", "mmap", "caps",
  };

  printf("REGION SIZE        OFFSET      CAPOFF FLAGS\n");
  for(__u32 i=0; i<num_regions; i++) {
    if (!region_info[i])
      continue;
    printf("%-6s %011llx %011llx %06x", region_name[i], region_info[i]->size, region_info[i]->offset, region_info[i]->cap_offset);
    print_flags(region_info[i]->flags, 4, NAMES);
    printf("\n");
  }
}

static
int
vfio_get_irq_info(int fd, __u32 index, struct vfio_irq_info **irq_info) {
  *irq_info = (struct vfio_irq_info *)malloc(sizeof(struct vfio_irq_info));
  (*irq_info)->argsz = sizeof(struct vfio_irq_info);
  (*irq_info)->index = index;
  int result = ioctl(fd, VFIO_DEVICE_GET_IRQ_INFO, *irq_info);
  if (result < 0)
    goto err;

  if((*irq_info)->argsz > sizeof(struct vfio_irq_info)) {
    *irq_info = (struct vfio_irq_info *)realloc(*irq_info, (*irq_info)->argsz);
    result = ioctl(fd, VFIO_DEVICE_GET_IRQ_INFO, *irq_info);
    if (result < 0)
      goto err;
  }
  return result;
 err:
  free(*irq_info);
  *irq_info = NULL;
  return result;
}

static const char *irq_name[] = {
  "intx",
  "msi",
  "msix",
  "err",
  "req",
};

static
void
vfio_print_irq_info(__u32 num_irqs, struct vfio_irq_info *irq_info[num_irqs]) {
  static char const* NAMES[] = {
    "eventfd", "maskable", "automasked", "noresize",
  };

  printf("IRQ  COUNT FLAGS\n");
  for(unsigned int i=0; i<num_irqs; i++) {
    if (!irq_info[i])
      continue;
    printf("%-4s %5u", irq_name[i], irq_info[i]->count);
    print_flags(irq_info[i]->flags, 4, NAMES);
    printf("\n");
  }
}

static
int
vfio_get_info(struct vfio_pci_dev *dev) {
  int result = vfio_get_device_info(dev->fd, &(dev->device_info));
  if (result < 0)
    return result;
  dev->region_info = (struct vfio_region_info **)calloc(dev->device_info->num_regions, sizeof(struct vfio_region_info *));
  for(__u32 i=0; i<dev->device_info->num_regions; i++)
    vfio_get_region_info(dev->fd, i, &(dev->region_info[i]));
  dev->irq_info = (struct vfio_irq_info **)calloc(dev->device_info->num_irqs, sizeof(struct vfio_irq_info *));
  for(__u32 i=0; i<dev->device_info->num_irqs; i++)
    vfio_get_irq_info(dev->fd, i, &(dev->irq_info[i]));
  return result;
}

void
vfio_print_info(struct vfio_pci_dev *dev) {
  vfio_print_iommu_info(dev->iommu_info);
  vfio_print_device_info(dev->device_info);
  vfio_print_region_info(dev->device_info->num_regions, dev->region_info);
  vfio_print_irq_info(dev->device_info->num_irqs, dev->irq_info);
}

static
void*
vfio_pci_dev_map_region(struct vfio_pci_dev *dev, size_t index) {
  if (index >= dev->device_info->num_regions)
    return NULL;
  __u32 flags = dev->region_info[index]->flags;
  if (!(flags & VFIO_REGION_INFO_FLAG_MMAP))
    return NULL;
  return mmap(NULL,
              dev->region_info[index]->size,
              ((flags & VFIO_REGION_INFO_FLAG_READ)?PROT_READ:0) |
              ((flags & VFIO_REGION_INFO_FLAG_WRITE)?PROT_WRITE:0),
              MAP_SHARED,
              dev->fd,
              dev->region_info[index]->offset);
}

static
int
vfio_pci_dev_unmap_region(struct vfio_pci_dev *dev, size_t index, void *addr) {
  if (index >= dev->device_info->num_regions)
    return 0;
  __u32 flags = dev->region_info[index]->flags;
  if (!(flags & VFIO_REGION_INFO_FLAG_MMAP))
    return 0;
  return munmap(addr, dev->region_info[index]->size);
}

void *
vfio_pci_dev_map_dma(struct vfio_pci_dev *dev, size_t size, __u64 *iova) {
  void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED)
    return addr;

  struct vfio_iommu_type1_dma_map dma_map = {
    .argsz = sizeof(dma_map),
    .vaddr = (__u64)addr,
    .size = size,
    .iova = (iova)?*iova:(__u64)addr,
    .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
  };

  if (ioctl(dev->container, VFIO_IOMMU_MAP_DMA, &dma_map) == 0) {
    if (iova)
      *iova += size;
    return addr;
  }

  munmap(addr, size);
  return MAP_FAILED;
}

int
vfio_pci_dev_unmap_dma(struct vfio_pci_dev *dev, size_t size, __u64 iova) {
  struct vfio_iommu_type1_dma_unmap dma_unmap = {
    .argsz = sizeof(dma_unmap),
    .size = size,
    .iova = iova,
    .flags = 0,
  };

  return ioctl(dev->container, VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
}

static
int
vfio_pci_dev_set_irqs(struct vfio_pci_dev *dev) {
  if (dev->num_irqs)
    return -1;

  __u32 indices[] = {
    VFIO_PCI_MSIX_IRQ_INDEX,
    VFIO_PCI_MSI_IRQ_INDEX,
    VFIO_PCI_INTX_IRQ_INDEX,
  };

  for(size_t i=0; i<sizeof(indices)/sizeof(indices[0]); i++) {
    struct vfio_irq_info *irq_info = dev->irq_info[indices[i]];
    if (!irq_info)
      continue;
    __u32 num = irq_info->count;
    if (!num)
      continue;

    char buf[sizeof(struct vfio_irq_set) + sizeof(int) * irq_info->count];
    struct vfio_irq_set *irq_set = (struct vfio_irq_set *)buf;

    irq_set->argsz = sizeof(buf);
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = indices[i];
    irq_set->start = 0;
    irq_set->count = num;

    dev->eventfd = (int *)malloc(num * sizeof(int));

    for(__u32 i=0; i<num; i++)
      dev->eventfd[i] = -1;

    for(__u32 i=0; i<num; i++) {
      int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
      if (fd < 0)
        return fd;

      dev->eventfd[i] = fd;
      ((int *)&irq_set->data)[i] = fd;
    }

    int result = ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, irq_set);
    if (result < 0)
      continue;
    dev->num_irqs = num;
    dev->irq_index = indices[i];
    return 0;
  }

  return -1;
}

static
int
vfio_pci_dev_clear_irqs(struct vfio_pci_dev *dev) {
  if (!(dev->num_irqs))
    return -1;

  struct vfio_irq_set irq_set = {
    .argsz = sizeof(struct vfio_irq_set),
    .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER,
    .index = dev->irq_index,
    .start = 0,
    .count = dev->num_irqs,
  };

  for(__u32 i=0; i<dev->num_irqs; i++)
    close(dev->eventfd[i]);
  free(dev->eventfd);
  dev->num_irqs = 0;

  return ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, &irq_set);
}


void
vfio_pci_dev_close(struct vfio_pci_dev *dev) {
  vfio_pci_dev_clear_irqs(dev);

  for(__u32 i=0; i<6; i++)
    if (dev->bar[i])
      vfio_pci_dev_unmap_region(dev, i, dev->bar[i]);

  if (dev->device_info) {
    if (dev->irq_info) {
      for(__u32 i=0; i<dev->device_info->num_irqs; i++)
        if (dev->irq_info[i])
          free(dev->irq_info[i]);
      free(dev->irq_info);
      dev->irq_info = NULL;
    }
    if (dev->region_info) {
      for(__u32 i=0; i<dev->device_info->num_regions; i++)
        if (dev->region_info[i])
          free(dev->region_info[i]);
      free(dev->region_info);
      dev->region_info = NULL;
    }
    free(dev->device_info);
    dev->device_info = NULL;
  }
  if (dev->iommu_info) {
    free(dev->iommu_info);
    dev->iommu_info = NULL;
  }
  if (dev->fd >= 0) {
    close(dev->fd);
    dev->fd = -1;
  }
  if (dev->container >= 0) {
    close(dev->container);
    dev->container = -1;
  }
}

int
vfio_pci_dev_open(const char *device, struct vfio_pci_dev *dev) {
  dev->container = -1;
  dev->fd = -1;
  dev->iommu_info = NULL;
  dev->device_info = NULL;
  dev->region_info = NULL;
  dev->irq_info = NULL;
  for(__u32 i=0; i<6; i++)
    dev->bar[i] = NULL;
  dev->num_irqs = 0;
  dev->eventfd = NULL;

  int dirfd = sysfs_pci_open_device(device);
  if (dirfd < 0)
    return dirfd;

  int driver = sysfs_pci_bind_driver(dirfd, device, "vfio-pci");
  if (driver < 0) {
    close(dirfd);
    return driver;
  }

  char buf[100];

  int result = readlinkat(dirfd, "iommu_group", buf, 100);
  if (result < 0) {
    close(dirfd);
    return result;
  }
  close(dirfd);

  int container = vfio_get_container_fd();
  if (container < 0)
    return container;
  result = -1;
  if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU))
    goto err1;

  result = vfio_get_group_fd(container, basename(buf));
  if (result < 0)
    goto err1;
  int group = result;
  result = ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
  if (result < 0)
    goto err2;

  result = vfio_get_iommu_info(container, &(dev->iommu_info));
  if (result < 0)
    goto err2;

  result = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, device);
  if (result < 0)
    goto err2;

  dev->fd = result;
  close(group);

  dev->container = container;
  result = vfio_get_info(dev);
  if (result < 0)
    goto err3;

  return result;
 err3:
  vfio_pci_dev_close(dev);
  return result;
 err2:
  close(group);
 err1:
  close(container);
  return result;
}

int
vfio_pci_dev_read_config(struct vfio_pci_dev *dev, void *buf, size_t count, off_t offset) {
  return pread(dev->fd, buf, count, dev->region_info[VFIO_PCI_CONFIG_REGION_INDEX]->offset + offset);
}

int
vfio_pci_dev_write_config(struct vfio_pci_dev *dev, const void *buf, size_t count, off_t offset) {
  return pwrite(dev->fd, buf, count, dev->region_info[VFIO_PCI_CONFIG_REGION_INDEX]->offset + offset);
}


int
vfio_pci_dev_init(struct vfio_pci_dev *dev) {
  int result = -1;
  for(size_t i=0; i<6; i++) {
    void *addr = vfio_pci_dev_map_region(dev, i);
    if (addr == MAP_FAILED)
      goto err;
    dev->bar[i] = addr;
  }

  uint16_t command;
  result = vfio_pci_dev_read_config(dev, &command, sizeof(command), PCI_COMMAND);
  if (result < 0)
    goto err;

  command |= PCI_COMMAND_MASTER;
  result = vfio_pci_dev_write_config(dev, &command, sizeof(command), PCI_COMMAND);
  if (result < 0)
    goto err;

  result = vfio_pci_dev_read_config(dev, &command, sizeof(command), PCI_COMMAND);
  if (result < 0)
    goto err;

  command |= PCI_COMMAND_INTX_DISABLE;
  result = vfio_pci_dev_write_config(dev, &command, sizeof(command), PCI_COMMAND);
  if (result < 0)
    goto err;

  result = vfio_pci_dev_read_config(dev, &command, sizeof(command), PCI_COMMAND);
  if (result < 0)
    goto err;

  if (dev->device_info->flags & VFIO_DEVICE_FLAGS_RESET) {
    result = ioctl(dev->fd, VFIO_DEVICE_RESET);
    if (result < 0)
      goto err;
  }

  result = vfio_pci_dev_set_irqs(dev);
  if (result < 0)
    goto err;

  return result;
 err:
  vfio_pci_dev_close(dev);
  return result;
}
