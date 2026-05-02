#pragma once

#include <stdint.h>
#include <sys/uio.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

#include "vfio.h"

struct virtio_queue {
  struct vring vring;
  uint32_t volatile *notify_addr;
  uint32_t index;
  uint16_t free_count;
  uint16_t free_head;

  uint16_t next_used;
  uint16_t used_index;
  uint16_t next_avail;
};

struct virtio_pci_dev {
  struct vfio_pci_dev *pci;
  struct virtio_pci_cap *caps[10];
  struct virtio_pci_common_cfg volatile *common_cfg;
  void volatile *device_cfg;
  struct virtio_queue *queues;
};

void
virtio_pci_dev_init(struct virtio_pci_dev *dev, struct vfio_pci_dev *pci, uint64_t feature);

void
virtio_send_driver_ok(struct virtio_pci_dev *dev);

int
virtio_queue_writev(struct virtio_queue *queue, const struct iovec *iov, int iovcnt, int readcnt);

void
virtio_queue_send(struct virtio_queue *queue, uint16_t index);

struct vring_used_elem *
virtio_queue_recv(struct virtio_queue *queue);

void
virtio_queue_free(struct virtio_queue *queue, uint16_t first);

void
virtio_queue_handle_event(struct virtio_queue *queue);
