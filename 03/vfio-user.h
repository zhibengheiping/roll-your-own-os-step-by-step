#pragma once

#include <stdint.h>

struct vfio_user_message_header {
  uint16_t message_id;
  uint16_t command;
  uint32_t size;
  uint32_t flags;
  uint32_t error;
};

#define VFIO_USER_FLAG_COMMAND 0x00
#define VFIO_USER_FLAG_REPLY   0x01
#define VFIO_USER_FLAG_NOREPLY 0x10
#define VFIO_USER_FLAG_ERROR   0x20

#define VFIO_USER_VERSION 1
#define VFIO_USER_DMA_MAP 2
#define VFIO_USER_DMA_UNMAP 3
#define VFIO_USER_DEVICE_GET_INFO 4
#define VFIO_USER_DEVICE_GET_REGION_INFO 5
#define VFIO_USER_DEVICE_GET_REGION_IO_FDS 6
#define VFIO_USER_DEVICE_GET_IRQ_INFO 7
#define VFIO_USER_DEVICE_SET_IRQS 8
#define VFIO_USER_REGION_READ 9
#define VFIO_USER_REGION_WRITE 10
#define VFIO_USER_DMA_READ 11
#define VFIO_USER_DMA_WRITE 12
#define VFIO_USER_DEVICE_RESET 13
#define VFIO_USER_REGION_WRITE_MULTI 15
#define VFIO_USER_DEVICE_FEATURE 16
#define VFIO_USER_MIG_DATA_READ 17
#define VFIO_USER_MIG_DATA_WRITE 18

struct vfio_user_version {
  uint16_t major;
  uint16_t minor;
  char data[];
};

struct vfio_user_dma_map_request {
  uint32_t argsz;
  uint32_t flags;
  uint64_t offset;
  uint64_t address;
  uint64_t size;
};

struct vfio_user_dma_unmap_request {
  uint32_t argsz;
  uint32_t flags;
  uint64_t address;
  uint64_t size;
};

struct vfio_user_device_info {
  uint32_t argsz;
  uint32_t flags;
  uint32_t num_regions;
  uint32_t num_irqs;
};

#define VFIO_USER_IO_FD_TYPE_IOEVENTFD 0
#define VFIO_USER_IO_FD_TYPE_IOREGIONFD 1

struct vfio_user_io_fd {
  uint64_t offset;
  uint64_t size;
  uint32_t fd_index;
  uint32_t type;
  uint32_t flags;
  uint32_t padding;
  uint64_t data;
};

struct vfio_user_region_io_fds {
  uint32_t argsz;
  uint32_t flags;
  uint32_t index;
  uint32_t count;
  struct vfio_user_io_fd subregions[];
};

struct vfio_user_region_readwrite {
  uint64_t offset;
  uint32_t region;
  uint32_t count;
  char data[];
};

struct vfio_user_dma_readwrite {
  uint64_t address;
  uint64_t count;
  char data[];
};

