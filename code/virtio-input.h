#pragma once

#include <linux/virtio_input.h>
#include "virtio.h"

struct virtio_input_dev {
  struct virtio_pci_dev virtio;
  struct virtio_input_event *events;
};

void
virtio_input_dev_init(struct virtio_input_dev *dev, struct vfio_pci_dev *pci);

int
virtio_input_recv(struct virtio_input_dev *dev, struct virtio_input_event *event);
