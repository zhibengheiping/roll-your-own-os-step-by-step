#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/vfio.h>
#include <linux/pci.h>
#include <json-c/json.h>

#include "vfio-user-dev.h"

static
int
recvfd(struct msghdr *msg, size_t count, int *fd) {
  if (msg->msg_controllen == 0)
    return 0;
  for (struct cmsghdr *hdr = CMSG_FIRSTHDR(msg); hdr != NULL; hdr = CMSG_NXTHDR(msg, hdr)) {
    if (hdr->cmsg_level != SOL_SOCKET)
      continue;
    if (hdr->cmsg_type != SCM_RIGHTS)
      continue;
    if (hdr->cmsg_len != CMSG_LEN(sizeof(int) * count))
      return -1;
    memcpy(fd, CMSG_DATA(hdr), sizeof(int) * count);
    break;
  }
  msg->msg_controllen = 0;
  return 0;
}

static
int
send_reply(int fd, size_t iovlen, struct iovec *iov) {
  size_t size = 0;
  for (size_t i=0; i<iovlen; ++i)
    size += iov[i].iov_len;

  struct msghdr msg = {
    .msg_iov = iov,
    .msg_iovlen = iovlen,
  };

  if (sendmsg(fd, &msg, MSG_WAITALL) < size)
    return -1;
  return 0;
}

static
void
pci_set_bar_address(uint32_t *bar, uint64_t size, uint32_t addr) {
  if (size == 0)
    return;
  *bar = addr & ~(size - 1);
}

static
int
handle_write_msi_cap(struct vfio_user_dev *dev, struct pci_cap_header *header, uint64_t offset, uint32_t count, void *data) {
  struct pci_msi_cap *cap = (struct pci_msi_cap *)header;
  switch (count) {
  case 2: {
    uint16_t *data16 = (uint16_t *)data;
    switch (offset) {
    case PCI_MSI_FLAGS:
      cap->flags = *data16;
      return 0;
    case PCI_MSI_DATA_32:
      cap->data_32 = *data16;
      return 0;
    }
  }
  case 4: {
    uint32_t *data32 = (uint32_t *)data;
    switch (offset) {
    case PCI_MSI_ADDRESS_LO:
      cap->address_lo = *data32;
      return 0;
    }
  }

  }
  return -1;
}

static
int
handle_write_pci_cfg(struct vfio_user_dev *dev, uint64_t offset, uint32_t count, void *data) {
  if (offset < sizeof(struct pci_cfg)) {
    switch (count) {
    case 4: {
      uint32_t *data32 = (uint32_t *)data;
      switch (offset) {
      case PCI_BASE_ADDRESS_0:
      case PCI_BASE_ADDRESS_1:
      case PCI_BASE_ADDRESS_2:
      case PCI_BASE_ADDRESS_3:
      case PCI_BASE_ADDRESS_4:
      case PCI_BASE_ADDRESS_5: {
        uint64_t index = (offset - PCI_BASE_ADDRESS_0) / 4;
        pci_set_bar_address(&dev->pci_cfg->bar[index], dev->regions[VFIO_PCI_BAR0_REGION_INDEX+index]->size, *data32);
        return 0;
      }
      case PCI_ROM_ADDRESS:
        return 0;
      }
    }
    case 2:
      switch (offset) {
      case PCI_COMMAND:
        uint16_t *data16 = (uint16_t *)data;
        dev->pci_cfg->command = *data16;
        return 0;
      }
    }
  } else if (dev->pci_cfg->status & PCI_STATUS_CAP_LIST) {
    uint8_t cap_start = dev->pci_cfg->capability_list;
    for (;;) {
      struct pci_cap_header *header = (struct pci_cap_header *)(((char *)dev->pci_cfg) + cap_start);
      uint8_t next_start = header->next_ptr;
      if ((next_start != 0) && (offset >= next_start)) {
        cap_start = next_start;
      } else {
        switch (header->cap_id) {
        case PCI_CAP_ID_MSI:
          if (handle_write_msi_cap(dev, header, offset - cap_start, count, data) == 0)
            return 0;
        }
        break;
      }
    }
  }

  return -1;
}

void
vfio_user_dev_trigger_irq(struct vfio_user_dev *dev, uint32_t index, uint32_t start) {
  int fd = dev->irqfds[index][start];
  uint64_t value = 1;
  write(fd, &value, sizeof(uint64_t));
}

static
int
add_dma_map(struct list_node *list, struct vfio_user_dev_dma_map *dma_map) {
  struct list_node *p = list->next;
  for (; p != list->prev; p = p->next) {
    struct vfio_user_dev_dma_map *node = (struct vfio_user_dev_dma_map *)p;
    if (node->address + node->size <= dma_map->address)
      continue;
    if (node->address < dma_map->address + dma_map->size)
      return -1;
    break;
  }

  list_insert_before(&dma_map->list, p);
  return 0;
}

static
int
remove_dma_map(struct list_node *list, uint64_t address, uint64_t size) {
  struct list_node *p = list->next;
  for (; p != list->prev; p = p->next) {
    struct vfio_user_dev_dma_map *node = (struct vfio_user_dev_dma_map *)p;
    if (node->address < address)
      continue;
    if (node->address > address)
      break;
    if (node->fd != -1)
      close(node->fd);
    list_remove(p);
    free(node);
    return 0;
  }

  return -1;
}

void
vfio_user_dev_dma_read(struct vfio_user_dev *dev, uint64_t addr, char *buf, uint64_t len) {
  struct list_node *list = &dev->dma_map_list;
  for (struct list_node *p = list->next; p != list->prev; p = p->next) {
    struct vfio_user_dev_dma_map *node = (struct vfio_user_dev_dma_map *)p;
    if (node->address + node->size <= addr)
      continue;
    if (node->fd < 0)
      continue;
    if (node->address > addr) {
      buf += (node->address - addr);
      len -= (node->address - addr);
      addr = node->address;
    }
    uint64_t size = node->address + node->size - addr;
    if (size > len)
      size = len;
    pread(node->fd, buf, size, node->offset + (addr - node->address));
    buf += size;
    addr += size;
    len -= size;
    if (len == 0)
      break;
  }
}

void
vfio_user_dev_dma_write(struct vfio_user_dev *dev, uint64_t addr, char const *buf, uint64_t len) {
  struct list_node *list = &dev->dma_map_list;
  for (struct list_node *p = list->next; p != list->prev; p = p->next) {
    struct vfio_user_dev_dma_map *node = (struct vfio_user_dev_dma_map *)p;
    if (node->address + node->size <= addr)
      continue;
    if (node->fd < 0)
      continue;
    if (node->address > addr) {
      buf += (node->address - addr);
      len -= (node->address - addr);
      addr = node->address;
    }
    uint64_t size = node->address + node->size - addr;
    if (size > len)
      size = len;
    pwrite(node->fd, buf, size, node->offset + (addr - node->address));
    buf += size;
    addr += size;
    len -= size;
    if (len == 0)
      break;
  }
}

void
vfio_user_dev_run(struct vfio_user_dev *dev) {
  list_init(&dev->dma_map_list);

  struct vfio_user_message_header header;
  if (recv(dev->fd, &header, sizeof(header), MSG_WAITALL) < sizeof(header))
    goto err;

  if (header.command != VFIO_USER_VERSION)
    goto err;

  int64_t max_msg_fds = 0;

  {
    char buffer[header.size-sizeof(header)];
    if (recv(dev->fd, buffer, sizeof(buffer), MSG_WAITALL) < sizeof(buffer))
      goto err;
    struct vfio_user_version *version = (struct vfio_user_version *)buffer;

    struct json_object *data = json_tokener_parse(version->data);
    struct json_object *caps = NULL;
    if (!json_object_object_get_ex(data, "capabilities", &caps))
      goto err;

    struct json_object *nfds = NULL;
    if (!json_object_object_get_ex(caps, "max_msg_fds", &nfds))
      goto err;

    if (json_object_get_type(nfds) != json_type_int)
      goto err;

    max_msg_fds = json_object_get_int64(nfds);
    json_object_object_del(caps, "twin_socket");

    char const *s = json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN);

    size_t size = strlen(s);
    header.flags = VFIO_USER_FLAG_REPLY;
    header.size = sizeof(header)+sizeof(struct vfio_user_version)+size+1;

    struct iovec iov[] = {
      {.iov_base = &header, .iov_len = sizeof(header)},
      {.iov_base = buffer, .iov_len = sizeof(struct vfio_user_version)},
      {.iov_base = (void *)s, .iov_len = size+1},
    };

    if (send_reply(dev->fd, 3, iov) < 0)
      goto err;
  }

  for (;;) {
    char control[CMSG_SPACE(sizeof(int) * max_msg_fds)];
    struct iovec recviov = {
      .iov_base = &header,
      .iov_len = sizeof(header),
    };
    struct msghdr msg = {
      .msg_iov = &recviov,
      .msg_iovlen = 1,
      .msg_control = control,
      .msg_controllen = sizeof(control),
    };

    if (recvmsg(dev->fd, &msg, MSG_WAITALL) < sizeof(header))
      goto err;

    char buffer[header.size-sizeof(header)];
    if (recv(dev->fd, buffer, sizeof(buffer), MSG_WAITALL) < sizeof(buffer))
      goto err;

    struct iovec iov[] = {
      {.iov_base = &header, .iov_len = sizeof(header)},
      {.iov_base = buffer, .iov_len = sizeof(buffer)},
      {.iov_base = NULL, .iov_len = 0},
    };

    size_t iovlen = sizeof(iov)/sizeof(iov[0]);

    uint32_t noreply = header.flags & (1 << 4);

    switch (header.command) {
    case VFIO_USER_DMA_MAP: {
      struct vfio_user_dma_map_request *req = (struct vfio_user_dma_map_request *)buffer;
      struct vfio_user_dev_dma_map *dma_map = malloc(sizeof(struct vfio_user_dev_dma_map));
      list_init(&dma_map->list);
      dma_map->flags = req->flags;
      dma_map->offset = req->offset;
      dma_map->address = req->address;
      dma_map->size = req->size;
      dma_map->fd = -1;

      if (recvfd(&msg, 1, &dma_map->fd) < 0)
        goto err;

      header.flags = VFIO_USER_FLAG_REPLY;
      header.size = sizeof(header);
      iovlen = 1;

      if (add_dma_map(&dev->dma_map_list, dma_map) < 0) {
        if (dma_map->fd != -1)
          close(dma_map->fd);
        free(dma_map);
        header.flags |= VFIO_USER_FLAG_ERROR;
        header.error = EINVAL;
      }

      break;
    }
    case VFIO_USER_DMA_UNMAP: {
      struct vfio_user_dma_unmap_request *req = (struct vfio_user_dma_unmap_request *)buffer;
      header.flags = VFIO_USER_FLAG_REPLY;
      header.size = sizeof(header);
      iovlen = 1;

      remove_dma_map(&dev->dma_map_list, req->address, req->size);
      break;
    }
    case VFIO_USER_DEVICE_GET_INFO: {
      struct vfio_user_device_info *info = (struct vfio_user_device_info *)buffer;

      header.flags = VFIO_USER_FLAG_REPLY;
      info->flags = VFIO_DEVICE_FLAGS_PCI;
      info->num_regions = dev->num_regions;
      info->num_irqs = dev->num_irqs;
      iovlen = 2;
      break;
    }
    case VFIO_USER_DEVICE_GET_REGION_INFO: {
      struct vfio_region_info *region = (struct vfio_region_info *)buffer;

      if (region->index >= dev->num_regions) {
        header.flags = VFIO_USER_FLAG_REPLY | VFIO_USER_FLAG_ERROR;
        header.size = sizeof(header);
        header.error = EINVAL;
        iovlen = 1;
      } else {
        header.flags = VFIO_USER_FLAG_REPLY;
        header.size = sizeof(header) + dev->regions[region->index]->argsz;
        iov[1].iov_base = dev->regions[region->index];
        iov[1].iov_len = dev->regions[region->index]->argsz;
        iovlen = 2;
      }
      break;
    }
    case VFIO_USER_DEVICE_GET_REGION_IO_FDS: {
      struct vfio_user_region_io_fds *region = (struct vfio_user_region_io_fds *)buffer;
      if (region->index >= dev->num_regions) {
        header.flags = VFIO_USER_FLAG_REPLY | VFIO_USER_FLAG_ERROR;
        header.size = sizeof(header);
        header.error = EINVAL;
        iovlen = 1;
      } else {
        header.flags = VFIO_USER_FLAG_REPLY;
        region->flags = 0;
        region->count = 0;
        iovlen = 2;
      }
      break;
    }
    case VFIO_USER_DEVICE_GET_IRQ_INFO: {
      struct vfio_irq_info *irq = (struct vfio_irq_info *)buffer;
      if (irq->index >= dev->num_irqs) {
        header.flags = VFIO_USER_FLAG_REPLY | VFIO_USER_FLAG_ERROR;
        header.size = sizeof(header);
        header.error = EINVAL;
        iovlen = 1;
      } else {
        header.flags = VFIO_USER_FLAG_REPLY;
        header.size = sizeof(header) + dev->irqs[irq->index].argsz;
        iov[1].iov_base = &dev->irqs[irq->index];
        iov[1].iov_len = dev->irqs[irq->index].argsz;
        iovlen = 2;
      }
      break;
    }
    case VFIO_USER_DEVICE_SET_IRQS: {
      struct vfio_irq_set *req = (struct vfio_irq_set *)buffer;
      if (req->index >= dev->num_irqs) {
        header.flags = VFIO_USER_FLAG_REPLY | VFIO_USER_FLAG_ERROR;
        header.size = sizeof(header);
        header.error = EINVAL;
        iovlen = 1;
      } else {
        int *irqfd = dev->irqfds[req->index] + req->start;
        for (uint32_t i=0; i<req->count; ++i) {
          if (irqfd[i] == -1)
            continue;
          close(irqfd[i]);
          irqfd[i] = -1;
        }

        if (recvfd(&msg, req->count, dev->irqfds[req->index]) < 0)
          goto err;

        header.flags = VFIO_USER_FLAG_REPLY;
        header.size = sizeof(header);
        iovlen = 1;
      }
      break;
    }
    case VFIO_USER_REGION_READ: {
      struct vfio_user_region_readwrite *rw = (struct vfio_user_region_readwrite *)buffer;
      switch (rw->region) {
      case VFIO_PCI_BAR0_REGION_INDEX:
      case VFIO_PCI_BAR1_REGION_INDEX:
      case VFIO_PCI_BAR2_REGION_INDEX:
      case VFIO_PCI_BAR3_REGION_INDEX:
      case VFIO_PCI_BAR4_REGION_INDEX:
      case VFIO_PCI_BAR5_REGION_INDEX: {
        uint32_t index = rw->region - VFIO_PCI_BAR0_REGION_INDEX;
        iov[2].iov_base = ((char *)dev->pci_bars[index]) + rw->offset;
        break;
      }
      case VFIO_PCI_CONFIG_REGION_INDEX: {
        iov[2].iov_base = ((char *)dev->pci_cfg) + rw->offset;
        break;
      }
      default:
        goto err;
      }

      header.flags = VFIO_USER_FLAG_REPLY;
      header.size += rw->count;
      iov[2].iov_len = rw->count;
      iovlen = 3;
      break;
    }
    case VFIO_USER_REGION_WRITE: {
      struct vfio_user_region_readwrite *rw = (struct vfio_user_region_readwrite *)buffer;

      switch (rw->region) {
      case VFIO_PCI_CONFIG_REGION_INDEX: {
        if (handle_write_pci_cfg(dev, rw->offset, rw->count, rw->data) < 0)
          goto err;
        break;
      }
      case VFIO_PCI_BAR0_REGION_INDEX:
      case VFIO_PCI_BAR1_REGION_INDEX:
      case VFIO_PCI_BAR2_REGION_INDEX:
      case VFIO_PCI_BAR3_REGION_INDEX:
      case VFIO_PCI_BAR4_REGION_INDEX:
      case VFIO_PCI_BAR5_REGION_INDEX: {
        uint32_t index = rw->region - VFIO_PCI_BAR0_REGION_INDEX;
        if (dev->handle_write_pci_bar(dev, index, rw->offset, rw->count, rw->data) < 0)
          goto err;
        break;
      }
      default:
        goto err;
      }
      header.flags = VFIO_USER_FLAG_REPLY;
      header.size -= rw->count;
      iov[1].iov_len -= rw->count;
      iovlen = 2;
      break;
    }
    default:
      goto err;
    }

    if (noreply)
      continue;

    if (send_reply(dev->fd, iovlen, iov) < 0)
      break;
  }
err:
  close(dev->fd);
  exit(1);
}
