#pragma once

#include <linux/virtio_gpu.h>
#include "virtio.h"

union virtio_gpu_request {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_resource_create_2d resource_create_2d;
  struct virtio_gpu_transfer_to_host_2d transfer_to_host_2d;
  struct {
    struct virtio_gpu_resource_attach_backing cmd;
    struct virtio_gpu_mem_entry entry;
  } resource_attach_backing;
  struct virtio_gpu_resource_flush resource_flush;
  struct virtio_gpu_set_scanout set_scanout;
};

union virtio_gpu_response {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_resp_display_info display_info;
};

struct virtio_gpu_cmd {
  union virtio_gpu_request request;
  union virtio_gpu_response response;
};


struct virtio_gpu_dev {
  struct virtio_pci_dev virtio;
  struct virtio_gpu_cmd *cmds;
  uint32_t fence_id;
};

void
virtio_gpu_dev_init(struct virtio_gpu_dev *dev, struct vfio_pci_dev *pci);

void
virtio_gpu_resource_create_2d(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height);

void
virtio_gpu_resource_attach_backing(struct virtio_gpu_dev *dev, uint32_t resource_id, void *buf, size_t size);

void
virtio_gpu_transfer_to_host_2d(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height);

void
virtio_gpu_resource_flush(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height);

void
virtio_gpu_set_scanout(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height);
