#pragma once

#include "vfio-user.h"
#include "list.h"

struct pci_cfg {
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t command;
  uint16_t status;
  uint8_t revision_id;
  uint8_t class_prog;
  uint16_t class_device;
  uint8_t cache_line_size;
  uint8_t latency_timer;
  uint8_t header_type;
  uint8_t bist;
  uint32_t bar[6];
  uint32_t cardbus_cis;
  uint16_t subsystem_vendor_id;
  uint16_t subsystem_id;
  uint32_t rom_address;
  uint8_t capability_list;
  uint8_t reserved[7];
  uint8_t interrupt_line;
  uint8_t interrupt_pin;
  uint8_t min_gnt;
  uint8_t max_lat;
};

struct pci_cap_header {
  uint8_t  cap_id;
  uint8_t  next_ptr;
};

struct pci_msi_cap {
  struct pci_cap_header header;
  uint16_t flags;
  uint32_t address_lo;
  union {
    uint32_t data_32;
    uint32_t address_hi;
  };
  union {
    uint32_t mask_32;
    uint32_t data_64;
  };
  union {
    uint32_t pending_32;
    uint32_t mask_64;
  };
  uint32_t pending_64;
};

struct vfio_user_dev_dma_map {
  struct list_node list;
  uint32_t flags;
  int fd;
  uint64_t offset;
  uint64_t address;
  uint64_t size;
};

struct vfio_user_dev {
  int fd;
  struct pci_cfg *pci_cfg;
  void volatile *pci_bars[6];
  uint32_t num_regions;
  struct vfio_region_info **regions;
  uint32_t num_irqs;
  struct vfio_irq_info *irqs;
  int **irqfds;
  struct list_node dma_map_list;
  int (*handle_write_pci_bar)(struct vfio_user_dev *dev, uint32_t bar, uint64_t offset, uint32_t count, void *data);
};


void
vfio_user_dev_trigger_irq(struct vfio_user_dev *dev, uint32_t index, uint32_t start);

void
vfio_user_dev_dma_read(struct vfio_user_dev *dev, uint64_t addr, char *buf, uint64_t len);

void
vfio_user_dev_dma_write(struct vfio_user_dev *dev, uint64_t addr, char const *buf, uint64_t len);

void
vfio_user_dev_run(struct vfio_user_dev *dev);
