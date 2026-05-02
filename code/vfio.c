#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/pci.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#include "vfio.h"

static
int
write_file_at(int dirfd, const char *filename, const char *id) {
  int fd = openat(dirfd, filename, O_WRONLY);
  if (fd < 0)
    return fd;
  ssize_t len = strlen(id);
  assert(write(fd, id, len) == len);
  close(fd);
  return 0;
}

static
int
open_at(const char *dirname, const char *name, int flags) {
  int dirfd = open(dirname, O_PATH);
  assert(dirfd >= 0);

  int fd = openat(dirfd, name, flags);
  assert(fd >= 0);
  close(dirfd);
  return fd;
}

static
int
sysfs_pci_open_device(const char *device) {
  return open_at("/sys/bus/pci/devices", device, O_PATH);
}

static
int
sysfs_pci_open_config(int devfd) {
  return openat(devfd, "config", O_RDONLY);
}

static
ssize_t
sysfs_pci_config_read_header_type(int fd, unsigned char *pci_header_type) {
  return pread(fd, pci_header_type, 1, 0x0E);
}

static
void
sysfs_pci_bind_driver(int devfd, const char *device, const char *driver) {
  assert((write_file_at(devfd, "driver/unbind", device) == 0) || (errno == ENOENT));
  assert(write_file_at(devfd, "driver_override", driver) == 0);
  int drvfd = open_at("/sys/bus/pci/drivers", driver, O_PATH);
  assert(write_file_at(drvfd, "bind", device) == 0);
  close(drvfd);
}

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
  assert(fd >= 0);
  assert (ioctl(fd, VFIO_GET_API_VERSION) == VFIO_API_VERSION);
  return fd;
}

static
int
vfio_get_group_fd(int container, const char *number) {
  int fd = open_at("/dev/vfio", number, O_RDWR);
  struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
  assert(ioctl(fd, VFIO_GROUP_GET_STATUS, &group_status) == 0);
  assert(group_status.flags & VFIO_GROUP_FLAGS_VIABLE);
  assert(ioctl(fd, VFIO_GROUP_SET_CONTAINER, &container) == 0);
  return fd;
}

static
void
vfio_get_iommu_info(int fd, struct vfio_iommu_type1_info **iommu_info) {
  *iommu_info = (struct vfio_iommu_type1_info *)malloc(sizeof(struct vfio_iommu_type1_info));
  (*iommu_info)->argsz = sizeof(struct vfio_iommu_type1_info);
  assert(ioctl(fd, VFIO_IOMMU_GET_INFO, *iommu_info) == 0);

  if ((*iommu_info)->argsz > sizeof(struct vfio_iommu_type1_info)) {
    *iommu_info = (struct vfio_iommu_type1_info *)realloc(*iommu_info, (*iommu_info)->argsz);
    assert(ioctl(fd, VFIO_IOMMU_GET_INFO, *iommu_info) == 0);
  }
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
void
vfio_get_device_info(int fd, struct vfio_device_info **device_info) {
  *device_info = (struct vfio_device_info *)malloc(sizeof(struct vfio_device_info));
  (*device_info)->argsz = sizeof(struct vfio_device_info);
  assert(ioctl(fd, VFIO_DEVICE_GET_INFO, *device_info) == 0);

  if((*device_info)->argsz > sizeof(struct vfio_device_info)) {
    *device_info = (struct vfio_device_info *)realloc(*device_info, (*device_info)->argsz);
    assert(ioctl(fd, VFIO_DEVICE_GET_INFO, *device_info) == 0);
  }
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
void
vfio_get_region_info(int fd, __u32 index, struct vfio_region_info **region_info) {
  *region_info = (struct vfio_region_info *)malloc(sizeof(struct vfio_region_info));
  (*region_info)->argsz = sizeof(struct vfio_region_info);
  (*region_info)->index = index;
  if (ioctl(fd, VFIO_DEVICE_GET_REGION_INFO, *region_info) < 0)
    return;

  if((*region_info)->argsz > sizeof(struct vfio_region_info)) {
    *region_info = (struct vfio_region_info *)realloc(*region_info, (*region_info)->argsz);
    assert(ioctl(fd, VFIO_DEVICE_GET_REGION_INFO, *region_info) == 0);
  }
}

static
const char *region_name[] = {
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

static
const char *irq_name[] = {
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
void
vfio_get_info(struct vfio_pci_dev *dev) {
  vfio_get_device_info(dev->fd, &(dev->device_info));
  dev->region_info = (struct vfio_region_info **)calloc(dev->device_info->num_regions, sizeof(struct vfio_region_info *));
  for(__u32 i=0; i<dev->device_info->num_regions; i++)
    vfio_get_region_info(dev->fd, i, &(dev->region_info[i]));
  dev->irq_info = (struct vfio_irq_info **)calloc(dev->device_info->num_irqs, sizeof(struct vfio_irq_info *));
  for(__u32 i=0; i<dev->device_info->num_irqs; i++)
    vfio_get_irq_info(dev->fd, i, &(dev->irq_info[i]));
}

static
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
  assert(addr != MAP_FAILED);

  struct vfio_iommu_type1_dma_map dma_map = {
    .argsz = sizeof(dma_map),
    .vaddr = (__u64)addr,
    .size = size,
    .iova = (iova)?*iova:(__u64)addr,
    .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
  };

  assert(ioctl(dev->container, VFIO_IOMMU_MAP_DMA, &dma_map) == 0);
  if (iova)
    *iova += size;
  return addr;
}

void
vfio_pci_dev_unmap_dma(struct vfio_pci_dev *dev, size_t size, __u64 iova) {
  struct vfio_iommu_type1_dma_unmap dma_unmap = {
    .argsz = sizeof(dma_unmap),
    .size = size,
    .iova = iova,
    .flags = 0,
  };

  assert(ioctl(dev->container, VFIO_IOMMU_UNMAP_DMA, &dma_unmap) == 0);
}

static
void
vfio_pci_dev_set_irqs(struct vfio_pci_dev *dev) {
  assert (dev->num_irqs == 0);

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
      assert(fd >= 0);
      dev->eventfd[i] = fd;
      ((int *)&irq_set->data)[i] = fd;
    }

    if (ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, irq_set) < 0)
      continue;
    dev->num_irqs = num;
    dev->irq_index = indices[i];
    return;
  }

  assert(0);
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
  if (dev->epollfd >= 0) {
    close(dev->epollfd);
    dev->epollfd = -1;
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

void
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
  sysfs_pci_bind_driver(dirfd, device, "vfio-pci");

  char buf[100];
  assert(readlinkat(dirfd, "iommu_group", buf, 100) > 0);
  close(dirfd);

  int container = vfio_get_container_fd();
  assert(ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU));

  int group = vfio_get_group_fd(container, basename(buf));
  assert(ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) == 0);

  vfio_get_iommu_info(container, &(dev->iommu_info));

  int fd = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, device);
  assert(fd >= 0);
  close(group);

  dev->fd = fd;
  dev->container = container;
  dev->epollfd = epoll_create1(EPOLL_CLOEXEC);
  vfio_get_info(dev);
  vfio_print_info(dev);
}

void
vfio_pci_dev_read_config(struct vfio_pci_dev *dev, void *buf, size_t count, off_t offset) {
  assert(pread(dev->fd, buf, count, dev->region_info[VFIO_PCI_CONFIG_REGION_INDEX]->offset + offset) == count);
}

void
vfio_pci_dev_write_config(struct vfio_pci_dev *dev, const void *buf, size_t count, off_t offset) {
  assert(pwrite(dev->fd, buf, count, dev->region_info[VFIO_PCI_CONFIG_REGION_INDEX]->offset + offset) == count);
}


void
vfio_pci_dev_init(struct vfio_pci_dev *dev) {
  int result = -1;
  for(size_t i=0; i<6; i++) {
    void *addr = vfio_pci_dev_map_region(dev, i);
    assert(addr != MAP_FAILED);
    dev->bar[i] = addr;
  }

  uint16_t command;
  vfio_pci_dev_read_config(dev, &command, sizeof(command), PCI_COMMAND);
  command |= PCI_COMMAND_MASTER;
  vfio_pci_dev_write_config(dev, &command, sizeof(command), PCI_COMMAND);
  vfio_pci_dev_read_config(dev, &command, sizeof(command), PCI_COMMAND);
  command |= PCI_COMMAND_INTX_DISABLE;
  vfio_pci_dev_write_config(dev, &command, sizeof(command), PCI_COMMAND);
  vfio_pci_dev_read_config(dev, &command, sizeof(command), PCI_COMMAND);

  vfio_pci_dev_set_irqs(dev);

  for (__u32 i=0; i<dev->num_irqs; ++i) {
    struct epoll_event e = {
      .events = EPOLLIN,
      .data = { .u64 = i },
    };

    assert(epoll_ctl(dev->epollfd, EPOLL_CTL_ADD, dev->eventfd[i], &e) == 0);
  }
}

int64_t
vfio_pci_dev_read_event(struct vfio_pci_dev *dev, int timeout) {
  struct epoll_event e;
  int n = epoll_wait(dev->epollfd, &e, 1, timeout);
  if (n == 0)
    return -1;
  return (__u32)e.data.u64;
}

void
vfio_pci_dev_clear_irq(struct vfio_pci_dev *dev, __u32 index) {
  char buf[8];
  assert(read(dev->eventfd[index], buf, 8) == 8);
}
