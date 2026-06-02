#include <assert.h>
#include <stdlib.h>
#include <string.h>

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

static
struct vring_used_elem *
virtio_gpu_recv(struct virtio_gpu_dev *dev) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  struct vring_used_elem *elem = virtio_queue_recv(queue);
  if (elem != NULL) {
    uint32_t flags = dev->cmds[elem->id].response.hdr.flags;
    if (flags & VIRTIO_GPU_FLAG_FENCE) {
      uint32_t fence_id = dev->cmds[elem->id].response.hdr.fence_id;
      dev->fence_completed = fence_id;
    }
  }

  return elem;
}

static
void
virtio_gpu_free(struct virtio_gpu_dev *dev, uint16_t index) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  virtio_queue_free(queue, index);
}

static
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
virtio_gpu_poll(struct virtio_gpu_dev *dev) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  struct vring_used_elem *elem = virtio_gpu_recv(dev);
  if (elem == NULL) {
    vfio_pci_dev_clear_irq(dev->virtio.pci, 0);
    virtio_queue_handle_event(queue);
  } else {
    virtio_gpu_free(dev, elem->id);
  }

  for (;;) {
    struct vring_used_elem *elem = virtio_gpu_recv(dev);
    if (elem == NULL)
      break;
    virtio_gpu_free(dev, elem->id);
  }
}

uint16_t
virtio_gpu_alloc(struct virtio_gpu_dev *dev, int iovcnt) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  for (;;) {
    int index = virtio_queue_alloc(queue, iovcnt);
    if (index >= 0)
      return index;
    virtio_gpu_poll(dev);
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


static
uint32_t
virtio_gpu_resource_alloc(struct virtio_gpu_dev *dev) {
  if (dev->free_rsc_id != 0) {
    uint32_t rsc_id = dev->free_rsc_id;
    dev->free_rsc_id = dev->rscs[rsc_id-1].next;
    return rsc_id;
  }

  if (dev->rscs_size == dev->rscs_cap) {
    dev->rscs_cap = dev->rscs_cap * 2;
    if (dev->rscs_cap == 0)
      dev->rscs_cap = 4;

    struct virtio_gpu_resource *rscs = realloc(dev->rscs, sizeof(struct virtio_gpu_resource) * dev->rscs_cap);
    assert(rscs != NULL);
    dev->rscs = rscs;
  }

  uint32_t rsc_id = dev->rscs_size;
  dev->rscs_size += 1;
  dev->rscs[rsc_id].next = 0;
  dev->rscs[rsc_id].buf = NULL;
  dev->rscs[rsc_id].size = 0;
  return rsc_id + 1;
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
  struct virtio_gpu_resp_display_info result = dev->cmds[id].response.display_info;
  virtio_gpu_free(dev, id);
  return result;
}

void
virtio_gpu_resource_unref(struct virtio_gpu_dev *dev, uint32_t resource_id) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.resource_unref,
      .iov_len = sizeof(dev->cmds[id].request.resource_unref)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .resource_unref = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_RESOURCE_UNREF,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .resource_id = resource_id,
      .padding = 0,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);

  dev->rscs[resource_id-1].next = dev->free_rsc_id;
  dev->free_rsc_id = resource_id;
}

uint32_t
virtio_gpu_resource_create_2d(struct virtio_gpu_dev *dev, uint32_t width, uint32_t height) {
  uint32_t resource_id = virtio_gpu_resource_alloc(dev);
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
  return resource_id;
}

void
virtio_gpu_resource_attach_backing(struct virtio_gpu_dev *dev, uint32_t resource_id, void *buf, size_t size) {
  dev->rscs[resource_id - 1].buf = buf;
  dev->rscs[resource_id - 1].size = size;

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

struct virtio_gpu_resp_capset_info
virtio_gpu_get_capset_info(struct virtio_gpu_dev *dev, uint32_t capset_index) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.get_capset_info,
      .iov_len = sizeof(dev->cmds[id].request.get_capset_info)},
    { .iov_base = &dev->cmds[id].response.capset_info,
      .iov_len = sizeof(dev->cmds[id].response.capset_info)},
  };

  union virtio_gpu_request request = {
    .get_capset_info = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_GET_CAPSET_INFO,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .capset_index = capset_index,
    },
  };

  dev->cmds[id].request = request;

  virtio_gpu_send(dev, id, iov, 2, 1);
  virtio_gpu_wait(dev, id);
  virtio_gpu_check_error(dev, id, VIRTIO_GPU_RESP_OK_CAPSET_INFO);
  struct virtio_gpu_resp_capset_info result = dev->cmds[id].response.capset_info;
  virtio_gpu_free(dev, id);
  return result;
}

void
virtio_gpu_get_capset(struct virtio_gpu_dev *dev, uint32_t capset_id, uint32_t capset_version, size_t size, void *capset_data) {
  uint16_t id = virtio_gpu_alloc(dev, 3);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.get_capset,
      .iov_len = sizeof(dev->cmds[id].request.get_capset)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
    { .iov_base = capset_data,
      .iov_len = size
    }
  };

  union virtio_gpu_request request = {
    .get_capset = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_GET_CAPSET,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .capset_id = capset_id,
      .capset_version = capset_version,
    },
  };

  dev->cmds[id].request = request;

  virtio_gpu_send(dev, id, iov, 3, 1);
  virtio_gpu_wait(dev, id);
  virtio_gpu_check_error(dev, id, VIRTIO_GPU_RESP_OK_CAPSET);
  virtio_gpu_free(dev, id);
}

uint32_t
virtio_gpu_resource_create_3d(struct virtio_gpu_dev *dev, struct virtio_gpu_resource_create_3d_args *args) {
  uint32_t rsc_id = virtio_gpu_resource_alloc(dev);

  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.resource_create_3d,
      .iov_len = sizeof(dev->cmds[id].request.resource_create_3d)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .resource_create_3d = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = 0,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .resource_id = rsc_id,
      .target = args->target,
      .format = args->format,
      .bind = args->bind,
      .width = args->width,
      .height = args->height,
      .depth = args->depth,
      .array_size = args->array_size,
      .last_level = args->last_level,
      .nr_samples = args->nr_samples,
      .flags = args->flags,
      .padding = 0,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
  return rsc_id;
}


void
virtio_gpu_transfer_from_host_3d(struct virtio_gpu_dev *dev, uint32_t ctx_id, struct virtio_gpu_transfer_host_3d_args *args) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.transfer_host_3d,
      .iov_len = sizeof(dev->cmds[id].request.transfer_host_3d)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .transfer_host_3d = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = ctx_id,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .box = {
        .x = args->x,
        .y = args->y,
        .z = args->z,
        .w = args->width,
        .h = args->height,
        .d = args->depth,
      },
      .offset = args->offset,
      .resource_id = args->resource_id,
      .level = args->level,
      .stride = args->stride,
      .layer_stride = args->layer_stride,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
}


static
uint32_t
virtio_gpu_ctx_alloc(struct virtio_gpu_dev *dev) {
  if (dev->free_ctx_id != 0) {
    uint32_t ctx_id = dev->free_ctx_id;
    dev->free_ctx_id = dev->ctxs[ctx_id-1].next;
    return ctx_id;
  }

  if (dev->ctxs_size == dev->ctxs_cap) {
    dev->ctxs_cap = dev->ctxs_cap * 2;
    if (dev->ctxs_cap == 0)
      dev->ctxs_cap = 4;

    struct virtio_gpu_ctx *ctxs = realloc(dev->ctxs, sizeof(struct virtio_gpu_ctx) * dev->ctxs_cap);
    assert(ctxs != NULL);
    dev->ctxs = ctxs;
  }

  uint32_t ctx_id = dev->ctxs_size;
  dev->ctxs_size += 1;
  dev->ctxs[ctx_id].next = 0;
  return ctx_id + 1;
}

static
uint32_t
virtio_gpu_ctx_fence_alloc(struct virtio_gpu_dev *dev, uint32_t ctx_id) {
  return ++(dev->fence_submitted);
}

uint32_t
virtio_gpu_ctx_create(struct virtio_gpu_dev *dev, uint32_t nlen, char const *debug_name) {
  uint32_t ctx_id = virtio_gpu_ctx_alloc(dev);

  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.ctx_create,
      .iov_len = sizeof(dev->cmds[id].request.ctx_create)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  nlen = (nlen>64)?64:nlen;

  union virtio_gpu_request request = {
    .ctx_create = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_CTX_CREATE,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = ctx_id,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .nlen = nlen,
      .context_init = 0,
    },
  };
  memcpy(request.ctx_create.debug_name, debug_name, nlen);

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);

  return ctx_id;
}

void
virtio_gpu_ctx_attach_resource(struct virtio_gpu_dev *dev, uint32_t ctx_id, uint32_t resource_id) {
  uint16_t id = virtio_gpu_alloc(dev, 2);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.ctx_resource,
      .iov_len = sizeof(dev->cmds[id].request.ctx_resource)},
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .ctx_resource = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
        .flags = 0,
        .fence_id = 0,
        .ctx_id = ctx_id,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .resource_id = resource_id,
      .padding = 0,
    },
  };
  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 2, 1);
}

void
virtio_gpu_submit_cmd(struct virtio_gpu_dev *dev, uint32_t ctx_id, size_t size, void *buf) {
  uint32_t fence_id = virtio_gpu_ctx_fence_alloc(dev, ctx_id);

  uint16_t id = virtio_gpu_alloc(dev, 3);
  struct iovec iov[] = {
    { .iov_base = &dev->cmds[id].request.cmd_submit,
      .iov_len = sizeof(dev->cmds[id].request.cmd_submit)},
    { .iov_base = buf,
      .iov_len = size },
    { .iov_base = &dev->cmds[id].response.hdr,
      .iov_len = sizeof(dev->cmds[id].response.hdr)},
  };

  union virtio_gpu_request request = {
    .cmd_submit = {
      .hdr = {
        .type = VIRTIO_GPU_CMD_SUBMIT_3D,
        .flags = VIRTIO_GPU_FLAG_FENCE,
        .fence_id = fence_id,
        .ctx_id = ctx_id,
        .ring_idx = 0,
        .padding = {0, 0, 0},
      },
      .size = size,
      .padding = 0,
    },
  };

  dev->cmds[id].request = request;
  virtio_gpu_sendrecv(dev, id, iov, 3, 2);
}



void
virtio_gpu_dev_init(struct virtio_gpu_dev *dev, struct vfio_pci_dev *pci) {
  virtio_pci_dev_init(&dev->virtio, pci, (1 << VIRTIO_GPU_F_VIRGL));
  /* virtio_pci_dev_init(&dev->virtio, pci, 0); */
  struct virtio_queue *queue = &dev->virtio.queues[0];
  size_t size = sizeof(struct virtio_gpu_cmd) * queue->vring.num;

  struct virtio_gpu_cmd *cmds = vfio_pci_dev_map_dma(pci, NULL, align_up(size, 4096), -1, 0);

  dev->cmds = cmds;
  dev->fence_id = 0;

  virtio_send_driver_ok(&dev->virtio);
  struct virtio_gpu_config volatile *cfg = dev->virtio.device_cfg;
  uint32_t num_capsets = cfg->num_capsets;

  for (uint32_t i=0; i<num_capsets; ++i) {
    struct virtio_gpu_resp_capset_info info = virtio_gpu_get_capset_info(dev, i);
    dev->capset_infos[info.capset_id-1].max_version = info.capset_max_version;
    dev->capset_infos[info.capset_id-1].max_size = info.capset_max_size;
  }

  dev->fence_submitted = 0;
  dev->fence_completed = 0;

  dev->ctxs_size = 0;
  dev->ctxs_cap = 0;
  dev->free_ctx_id = 0;
  dev->ctxs = NULL;


  dev->rscs_size = 0;
  dev->rscs_cap = 0;
  dev->free_rsc_id = 0;
  dev->rscs = NULL;
}
