#include <assert.h>
#include <stdlib.h>

#include <stdio.h>

#include "virtio-gpu.h"

static
size_t
align_down(size_t addr, size_t align) {
  return addr - addr % align;
}

static
size_t
align_up(size_t addr, size_t align) {
  return align_down(addr + align - 1, align);
}

struct vring_used_elem *
virtio_gpu_recv(struct virtio_gpu_dev *dev) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  struct vring_used_elem *elem = virtio_queue_recv(queue);
  if (elem != NULL) {
    uint32_t flags = dev->cmds[elem->id].response.hdr.flags;
    if (flags & VIRTIO_GPU_FLAG_FENCE) {
      /* assert(0); */
    }
  }

  return elem;
}

void
virtio_gpu_free(struct virtio_gpu_dev *dev, uint16_t index) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  virtio_queue_free(queue, index);
}

void
virtio_gpu_wait(struct virtio_gpu_dev *dev, uint16_t id) {
  struct virtio_queue *queue = &dev->virtio.queues[0];

  for (int found=0;!found;) {
    vfio_pci_dev_clear_irq(dev->virtio.pci, 0);
    virtio_queue_handle_event(queue);
    for (;;) {
      struct vring_used_elem *elem = virtio_gpu_recv(dev);
      if (elem == NULL)
        break;

      if (elem->id == id) {
        found = 1;
        continue;
      }
      virtio_gpu_free(dev, elem->id);
    }
  }
}

void
virtio_gpu_wait_free(struct virtio_gpu_dev *dev) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  for (;;) {
    struct vring_used_elem *elem = virtio_gpu_recv(dev);
    if (elem == NULL)
      break;
    virtio_gpu_free(dev, elem->id);
  }
  vfio_pci_dev_clear_irq(dev->virtio.pci, 0);
  virtio_queue_handle_event(queue);
}

uint16_t
virtio_gpu_alloc(struct virtio_gpu_dev *dev, int iovcnt) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  for (;;) {
    int index = virtio_queue_alloc(queue, iovcnt);
    if (index >= 0)
      return index;
    virtio_gpu_wait_free(dev);
  }
}

void
virtio_gpu_send(struct virtio_gpu_dev *dev, uint16_t id, struct iovec *iov, int iovcnt, int readcnt) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  virtio_queue_writev(queue, id, iov, iovcnt, readcnt);
  virtio_queue_send(queue, id);
}

void
virtio_gpu_check_error(struct virtio_gpu_dev *dev, uint16_t id, enum virtio_gpu_ctrl_type ok) {
  enum virtio_gpu_ctrl_type type = dev->cmds[id].response.hdr.type;
  assert(type == ok);
}

void
virtio_gpu_sendrecv(struct virtio_gpu_dev *dev, uint16_t id, struct iovec *iov, int iovcnt, int readcnt) {
  virtio_gpu_send(dev, id, iov, iovcnt, readcnt);
  virtio_gpu_wait(dev, id);
  virtio_gpu_check_error(dev, id, VIRTIO_GPU_RESP_OK_NODATA);
  virtio_gpu_free(dev, id);
}

struct virtio_gpu_resp_display_info
virtio_gpu_get_display_info(struct virtio_gpu_dev *dev) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.hdr,
      .iov_len = sizeof(dev->cmds[id].request.hdr)},
    { .iov_base = &dev->cmds[id].response.display_info,
      .iov_len = sizeof(dev->cmds[id].response.display_info)},
  };

  union virtio_gpu_request request = {
    .hdr = {
      .type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO,
      .flags = 0,
      .fence_id = 0,
      .ctx_id = 0,
      .ring_idx = 0,
      .padding = {0, 0, 0},
    },
  };

  dev->cmds[id].request = request;

  virtio_gpu_send(dev, id, iov, 2, 1);
  virtio_gpu_wait(dev, id);
  virtio_gpu_check_error(dev, id, VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
  return dev->cmds[id].response.display_info;
}

void
virtio_gpu_resource_create_2d(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.resource_create_2d,
      .iov_len = sizeof(dev->cmds[id].request.resource_create_2d)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .resource_create_2d = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .resource_id = resource_id,
      .format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
      .width = width,
      .height = height,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
}

void
virtio_gpu_resource_attach_backing(struct virtio_gpu_dev *dev, uint32_t resource_id, void *buf, size_t size) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.resource_attach_backing,
      .iov_len = sizeof(dev->cmds[id].request.resource_attach_backing)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  struct virtio_gpu_resource_attach_backing cmd = {
    .hdr = {
      .type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
      .flags = 0,
      .fence_id = 0,
      .ctx_id = 0,
      .ring_idx = 0,
      .padding = {0, 0, 0},
    },
    .resource_id = resource_id,
    .nr_entries = 1,
  };
  struct virtio_gpu_mem_entry entry = {
    .addr = (uintptr_t)buf,
    .length = size,
    .padding = 0,
  };

  dev->cmds[id].request.resource_attach_backing.cmd = cmd;
  dev->cmds[id].request.resource_attach_backing.entry = entry;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
}

void
virtio_gpu_set_scanout(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.set_scanout,
      .iov_len = sizeof(dev->cmds[id].request.set_scanout)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .set_scanout = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_SET_SCANOUT,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .r = {
        .x = 0,
        .y = 0,
        .width = width,
        .height = height,
      },
      .scanout_id = 0,
      .resource_id = resource_id,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
}

void
virtio_gpu_transfer_to_host_2d(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.transfer_to_host_2d,
      .iov_len = sizeof(dev->cmds[id].request.transfer_to_host_2d)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .transfer_to_host_2d = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .r = {
        .x = 0,
        .y = 0,
        .width = width,
        .height = height,
      },
      .offset = 0,
      .resource_id = resource_id,
      .padding = 0,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
}

void
virtio_gpu_resource_flush(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.resource_flush,
      .iov_len = sizeof(dev->cmds[id].request.resource_flush)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  uint32_t fence_id = dev->fence_id;
  ++(dev->fence_id);

  union virtio_gpu_request request = {
    .resource_flush = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_RESOURCE_FLUSH,
        .flags = VIRTIO_GPU_FLAG_FENCE,
        .fence_id = fence_id,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .r = {
        .x = 0,
        .y = 0,
        .width = width,
        .height = height,
      },
      .resource_id = resource_id,
      .padding = 0,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
}


void
virtio_gpu_dev_init(struct virtio_gpu_dev *dev, struct vfio_pci_dev *pci) {
  virtio_pci_dev_init(&dev->virtio, pci, (1 << VIRTIO_GPU_F_VIRGL));
  virtio_pci_dev_init(&dev->virtio, pci, 0);
  struct virtio_queue *queue = &dev->virtio.queues[0];
  size_t size = sizeof(struct virtio_gpu_cmd) * queue->vring.num;

  struct virtio_gpu_cmd *cmds = vfio_pci_dev_map_dma(pci, align_up(size, 4096), NULL);

  dev->cmds = cmds;
  dev->fence_id = 0;

  virtio_send_driver_ok(&dev->virtio);
  struct virtio_gpu_resp_display_info info = virtio_gpu_get_display_info(dev);
  printf("%u %u %u %u\n", info.pmodes[0].r.x, info.pmodes[0].r.y, info.pmodes[0].r.width, info.pmodes[0].r.height);
}
