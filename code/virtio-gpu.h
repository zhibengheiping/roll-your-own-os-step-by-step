#pragma once

#include <linux/virtio_gpu.h>
#include "virtio.h"

struct virtio_gpu_resource_create_3d_args {
  uint32_t target;
  uint32_t format;
  uint32_t bind;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t array_size;
  uint32_t last_level;
  uint32_t nr_samples;
  uint32_t flags;
};

struct virtio_gpu_transfer_host_3d_args {
  uint32_t x;
  uint32_t y;
  uint32_t z;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t offset;
  uint32_t resource_id;
  uint32_t level;
  uint32_t stride;
  uint32_t layer_stride;
};

union virtio_gpu_request {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_resource_unref resource_unref;
  struct virtio_gpu_resource_create_2d resource_create_2d;
  struct virtio_gpu_transfer_to_host_2d transfer_to_host_2d;
  struct {
    struct virtio_gpu_resource_attach_backing cmd;
    struct virtio_gpu_mem_entry entry;
  } resource_attach_backing;
  struct virtio_gpu_resource_flush resource_flush;
  struct virtio_gpu_set_scanout set_scanout;
  struct virtio_gpu_get_capset_info get_capset_info;
  struct virtio_gpu_get_capset get_capset;
  struct virtio_gpu_resource_create_3d resource_create_3d;
  struct virtio_gpu_transfer_host_3d transfer_host_3d;
  struct virtio_gpu_ctx_create ctx_create;
  struct virtio_gpu_ctx_resource ctx_resource;
  struct virtio_gpu_cmd_submit cmd_submit;
};

union virtio_gpu_response {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_resp_display_info display_info;
  struct virtio_gpu_resp_capset_info capset_info;
};

struct virtio_gpu_cmd {
  union virtio_gpu_request request;
  union virtio_gpu_response response;
};

struct virtio_gpu_capset_info {
  uint32_t max_version;
  uint32_t max_size;
};

struct virtio_gpu_ctx {
  uint32_t next;
};

struct virtio_gpu_resource {
  uint32_t next;
  void *buf;
  size_t size;
};

struct virtio_gpu_dev {
  struct virtio_pci_dev virtio;
  struct virtio_gpu_cmd *cmds;
  uint32_t fence_id;
  struct virtio_gpu_capset_info capset_infos[6];
  uint32_t fence_submitted;
  uint32_t fence_completed;
  uint32_t ctxs_size;
  uint32_t ctxs_cap;
  uint32_t free_ctx_id;
  struct virtio_gpu_ctx *ctxs;
  uint32_t rscs_size;
  uint32_t rscs_cap;
  uint32_t free_rsc_id;
  struct virtio_gpu_resource *rscs;
};

void
virtio_gpu_dev_init(struct virtio_gpu_dev *dev, struct vfio_pci_dev *pci);

void
virtio_gpu_poll(struct virtio_gpu_dev *dev);

uint32_t
virtio_gpu_resource_create_2d(struct virtio_gpu_dev *dev, uint32_t width, uint32_t height);

void
virtio_gpu_resource_unref(struct virtio_gpu_dev *dev, uint32_t resource_id);

void
virtio_gpu_resource_attach_backing(struct virtio_gpu_dev *dev, uint32_t resource_id, void *buf, size_t size);

void
virtio_gpu_transfer_to_host_2d(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height);

void
virtio_gpu_resource_flush(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height);

void
virtio_gpu_set_scanout(struct virtio_gpu_dev *dev, uint32_t resource_id, uint32_t width, uint32_t height);

struct virtio_gpu_resp_capset_info
virtio_gpu_get_capset_info(struct virtio_gpu_dev *dev, uint32_t capset_index);

void
virtio_gpu_get_capset(struct virtio_gpu_dev *dev, uint32_t capset_id, uint32_t capset_version, size_t size, void *capset_data);

uint32_t
virtio_gpu_resource_create_3d(struct virtio_gpu_dev *dev, struct virtio_gpu_resource_create_3d_args *args);

void
virtio_gpu_transfer_from_host_3d(struct virtio_gpu_dev *dev, uint32_t ctx_id, struct virtio_gpu_transfer_host_3d_args *args);

uint32_t
virtio_gpu_ctx_create(struct virtio_gpu_dev *dev, uint32_t nlen, char const *debug_name);

void
virtio_gpu_ctx_attach_resource(struct virtio_gpu_dev *dev, uint32_t ctx_id, uint32_t resource_id);

void
virtio_gpu_submit_cmd(struct virtio_gpu_dev *dev, uint32_t ctx_id, size_t size, void *buf);
