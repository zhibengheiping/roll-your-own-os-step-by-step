#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <linux/vfio.h>
#include <linux/pci.h>

#include "edu.h"
#include "vfio-user-dev.h"

struct edu_dev_pci_cfg {
  struct pci_cfg cfg;
  struct pci_msi_cap msi_cap;
};

struct edu_dev {
  struct vfio_user_dev dev;
  char *buffer;
};

int
wait_connection(char const *path) {
  int listen_fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (listen_fd < 0)
    return listen_fd;

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  int result = bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
  if (result < 0)
    goto exit;

  result = listen(listen_fd, 1);
  if (result < 0)
    goto exit;

  result = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
exit:
  close(listen_fd);
  unlink(path);
  return result;
}

static
void *
compute_factorial(void *arg) {
  struct vfio_user_dev *dev = arg;
  struct edu_cfg volatile *edu_cfg = dev->pci_bars[0];
  uint32_t factorial = atomic_load(&edu_cfg->factorial);
  uint32_t result = 1;
  for (;factorial;--factorial)
    result *= factorial;
  sleep(1);

  atomic_store(&edu_cfg->factorial, result);
  uint32_t factorial_status;
  for (;;) {
    factorial_status = atomic_load(&edu_cfg->factorial_status);
    if (!(factorial_status & 1))
      break;
    if (!atomic_compare_exchange_strong(&edu_cfg->factorial_status, &factorial_status, factorial_status^1))
      continue;
  }

  if (!(factorial_status & 0x80))
    return NULL;

  for (;;) {
    uint32_t expected = atomic_load(&edu_cfg->interrupt_status);
    if (expected & 1)
      break;
    if (!atomic_compare_exchange_strong(&edu_cfg->interrupt_status, &expected, expected|1))
      continue;
  }

  vfio_user_dev_trigger_irq(dev, VFIO_PCI_MSI_IRQ_INDEX, 0);
  return NULL;
}

static
void *
copy_from_ram(void *arg) {
  struct edu_dev *dev = arg;
  struct edu_cfg volatile *edu_cfg = dev->dev.pci_bars[0];
  uint64_t command = atomic_load(&edu_cfg->dma_command);
  uint64_t dma_src = atomic_load(&edu_cfg->dma_src);
  uint64_t dma_dst = atomic_load(&edu_cfg->dma_dst);
  uint64_t dma_len = atomic_load(&edu_cfg->dma_len);

  vfio_user_dev_dma_read(&dev->dev, dma_src, dev->buffer+dma_dst, dma_len);
  atomic_store(&edu_cfg->dma_command, command^1);

  if (command & 4) {
    for (;;) {
      uint32_t expected = atomic_load(&edu_cfg->interrupt_status);
      if (expected|0x100)
        break;
      if (atomic_compare_exchange_strong(&edu_cfg->interrupt_status, &expected, expected|0x100))
        break;
    }
    vfio_user_dev_trigger_irq(&dev->dev, VFIO_PCI_MSI_IRQ_INDEX, 0);
  }

  return NULL;
}

static
void *
copy_to_ram(void *arg) {
  struct edu_dev *dev = arg;
  struct edu_cfg volatile *edu_cfg = dev->dev.pci_bars[0];
  uint64_t command = atomic_load(&edu_cfg->dma_command);
  uint64_t dma_src = atomic_load(&edu_cfg->dma_src);
  uint64_t dma_dst = atomic_load(&edu_cfg->dma_dst);
  uint64_t dma_len = atomic_load(&edu_cfg->dma_len);

  vfio_user_dev_dma_write(&dev->dev, dma_dst, dev->buffer+dma_src, dma_len);
  atomic_store(&edu_cfg->dma_command, command^1);

  if (command & 4) {
    for (;;) {
      uint32_t expected = atomic_load(&edu_cfg->interrupt_status);
      if (expected|0x100)
        break;
      if (atomic_compare_exchange_strong(&edu_cfg->interrupt_status, &expected, expected|0x100))
        break;
    }
    vfio_user_dev_trigger_irq(&dev->dev, VFIO_PCI_MSI_IRQ_INDEX, 0);
  }

  return NULL;
}

int
handle_write_pci_bar(struct vfio_user_dev *dev, uint32_t bar, uint64_t offset, uint32_t count, void *data) {
  if (bar != 0)
    return -1;
  struct edu_cfg volatile *edu_cfg = dev->pci_bars[0];
  switch (count) {
  case 4: {
    uint32_t *data32 = (uint32_t *)data;
    switch (offset) {
    case offsetof(struct edu_cfg, liveness):
      edu_cfg->liveness = ~*data32;
      return 0;
    case offsetof(struct edu_cfg, factorial):
      for (;;) {
        uint32_t expected = atomic_load(&edu_cfg->factorial_status);
        if (expected & 1)
          break;
        if (!atomic_compare_exchange_strong(&edu_cfg->factorial_status, &expected, expected|1))
          continue;
        atomic_store(&edu_cfg->factorial, *data32);
        pthread_t thread;
        pthread_create(&thread, NULL, compute_factorial, dev);
        break;
      }
      return 0;
    case offsetof(struct edu_cfg, factorial_status): {
      for (;;) {
        uint32_t expected = atomic_load(&edu_cfg->factorial_status);
        if (atomic_compare_exchange_strong(&edu_cfg->factorial_status, &expected, (expected & 1)|*data32))
          break;
      }
      return 0;
    }
    case offsetof(struct edu_cfg, interrupt_acknowledge):
      for (;;) {
        uint32_t expected = atomic_load(&edu_cfg->interrupt_status);
        if (atomic_compare_exchange_strong(&edu_cfg->interrupt_status, &expected, expected^*data32))
          break;
      }
      return 0;
    }
    break;
  }
  case 8: {
    uint64_t *data64 = (uint64_t *)data;
    switch (offset) {
    case offsetof(struct edu_cfg, dma_src): {
      edu_cfg->dma_src = *data64;
      return 0;
    }
    case offsetof(struct edu_cfg, dma_dst): {
      edu_cfg->dma_dst = *data64;
      return 0;
    }
    case offsetof(struct edu_cfg, dma_len): {
      edu_cfg->dma_len = *data64;
      return 0;
    }
    case offsetof(struct edu_cfg, dma_command): {
      uint64_t command = *data64;
      for (;;) {
        uint64_t expected = atomic_load(&edu_cfg->dma_command);
        if (expected & 1)
          break;
        if (!atomic_compare_exchange_strong(&edu_cfg->dma_command, &expected, command))
          continue;
        pthread_t thread;
        if (command & 1)
          pthread_create(&thread, NULL, (command & 2)?copy_to_ram:copy_from_ram, dev);
        break;
      }
      return 0;
    }
    }
    break;
  }
  }
  return -1;
}

int
main(int argc, char const **argv) {
  if (argc < 2)
    return 1;

  int fd = wait_connection(argv[1]);
  if (fd < 0)
    return 1;

  struct edu_dev_pci_cfg pci_cfg = {
    .cfg = {
      .vendor_id = 0x1234,
      .device_id = 0x11e8,
      .command = 0,
      .status = PCI_STATUS_CAP_LIST,
      .revision_id = 0x10,
      .class_prog = 0x00,
      .class_device = 0xFF,
      .cache_line_size = 0,
      .latency_timer = 0,
      .header_type = PCI_HEADER_TYPE_NORMAL,
      .bist = 0,
      .bar = {0, 0, 0, 0, 0, 0},
      .cardbus_cis = 0,
      .subsystem_vendor_id = 0x1af4,
      .subsystem_id = 0x1100,
      .rom_address = 0,
      .capability_list = offsetof(struct edu_dev_pci_cfg, msi_cap) - offsetof(struct edu_dev_pci_cfg, cfg),
      .reserved = {0, 0, 0, 0, 0, 0, 0},
      .interrupt_line = 0xFF,
      .interrupt_pin = 0x0,
      .min_gnt = 0,
      .max_lat = 0,
    },
    .msi_cap = {
      .header = {
        .cap_id = PCI_CAP_ID_MSI,
        .next_ptr = 0,
      },
      .flags = 0,
    },
  };

  struct vfio_region_info bar0_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE,
    .index = VFIO_PCI_BAR0_REGION_INDEX,
    .cap_offset = 0,
    .size = 0x1000,
    .offset = 0x00000000000,
  };

  struct vfio_region_info bar1_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = 0,
    .index = VFIO_PCI_BAR1_REGION_INDEX,
    .cap_offset = 0,
    .size = 0,
    .offset = 0x10000000000,
  };

  struct vfio_region_info bar2_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = 0,
    .index = VFIO_PCI_BAR2_REGION_INDEX,
    .cap_offset = 0,
    .size = 0,
    .offset = 0x20000000000,
  };

  struct vfio_region_info bar3_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = 0,
    .index = VFIO_PCI_BAR3_REGION_INDEX,
    .cap_offset = 0,
    .size = 0,
    .offset = 0x30000000000,
  };

  struct vfio_region_info bar4_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = 0,
    .index = VFIO_PCI_BAR4_REGION_INDEX,
    .cap_offset = 0,
    .size = 0,
    .offset = 0x40000000000,
  };

  struct vfio_region_info bar5_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = 0,
    .index = VFIO_PCI_BAR5_REGION_INDEX,
    .cap_offset = 0,
    .size = 0,
    .offset = 0x50000000000,
  };

  struct vfio_region_info rom_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = 0,
    .index = VFIO_PCI_ROM_REGION_INDEX,
    .cap_offset = 0,
    .size = 0,
    .offset = 0x60000000000,
  };

  struct vfio_region_info config_info = {
    .argsz = sizeof(struct vfio_region_info),
    .flags = VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE,
    .index = VFIO_PCI_CONFIG_REGION_INDEX,
    .cap_offset = 0,
    .size = sizeof(pci_cfg),
    .offset = 0x70000000000,
  };

  struct vfio_region_info*regions[] = {
    [VFIO_PCI_BAR0_REGION_INDEX] = &bar0_info,
    [VFIO_PCI_BAR1_REGION_INDEX] = &bar1_info,
    [VFIO_PCI_BAR2_REGION_INDEX] = &bar2_info,
    [VFIO_PCI_BAR3_REGION_INDEX] = &bar3_info,
    [VFIO_PCI_BAR4_REGION_INDEX] = &bar4_info,
    [VFIO_PCI_BAR5_REGION_INDEX] = &bar5_info,
    [VFIO_PCI_ROM_REGION_INDEX] = &rom_info,
    [VFIO_PCI_CONFIG_REGION_INDEX] = &config_info,
  };

  struct vfio_irq_info irqs[] = {
    [VFIO_PCI_INTX_IRQ_INDEX] = {
      .flags = VFIO_IRQ_INFO_EVENTFD|VFIO_IRQ_INFO_NORESIZE,
      .count = 0,
    },
    [VFIO_PCI_MSI_IRQ_INDEX] = {
      .flags=VFIO_IRQ_INFO_EVENTFD|VFIO_IRQ_INFO_NORESIZE,
      .count = 1,
    },
    [VFIO_PCI_MSIX_IRQ_INDEX] = {
      .flags = VFIO_IRQ_INFO_EVENTFD|VFIO_IRQ_INFO_NORESIZE,
      .count = 0,
    },
    [VFIO_PCI_ERR_IRQ_INDEX] = {
      .flags = VFIO_IRQ_INFO_EVENTFD|VFIO_IRQ_INFO_NORESIZE,
      .count = 1,
    },
    [VFIO_PCI_REQ_IRQ_INDEX] = {
      .flags = VFIO_IRQ_INFO_EVENTFD|VFIO_IRQ_INFO_NORESIZE,
      .count = 1,
    },
  };

  int msi_irqfd = -1;
  int err_irqfd = -1;
  int req_irqfd = -1;


  int *irqfds[] = {
    [VFIO_PCI_INTX_IRQ_INDEX] = NULL,
    [VFIO_PCI_MSI_IRQ_INDEX] = &msi_irqfd,
    [VFIO_PCI_MSIX_IRQ_INDEX] = NULL,
    [VFIO_PCI_ERR_IRQ_INDEX] = &err_irqfd,
    [VFIO_PCI_REQ_IRQ_INDEX] = &req_irqfd,
  };

  struct edu_cfg volatile edu_cfg = {
    .identification = 0x010000EDU,
    .liveness = 0,
  };

  struct edu_dev dev = {
    .dev = {
      .fd = fd,
      .pci_cfg = &pci_cfg.cfg,
      .pci_bars = {&edu_cfg, NULL, NULL, NULL, NULL, NULL},
      .num_regions = sizeof(regions)/sizeof(regions[0]),
      .regions = regions,
      .num_irqs = sizeof(irqs)/sizeof(irqs[0]),
      .irqs = irqs,
      .irqfds = irqfds,
      .handle_write_pci_bar = handle_write_pci_bar,
    },
    .buffer = mmap(NULL, 0x10000000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0),
  };

  vfio_user_dev_run(&dev.dev);

  return 0;
}
