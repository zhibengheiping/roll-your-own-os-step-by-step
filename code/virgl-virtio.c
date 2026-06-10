#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>

#include "virtio-gpu.h"
#include "vtest_protocol.h"

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

struct vtest_header {
  uint32_t length;
  uint32_t command;
};

static
void
init_context(struct virtio_gpu_dev *dev, uint32_t *ctx_id, size_t nlen, char const *debug_name) {
  if (*ctx_id)
    return;

  *ctx_id = virtio_gpu_ctx_create(dev, nlen, debug_name);
}

static
void
sendall(int fd, void *buf, size_t size) {
  char *p = buf;
  while (size > 0) {
    ssize_t sent = write(fd, p, size);
    if (send < 0) {
      if (errno == EINTR)
        continue;
      assert(0);
    }
    size -= sent;
    p += sent;
  }
}

static
void
send_memfd(int fd, int memfd) {
  char c = 0;
  char control[CMSG_SPACE(sizeof(int))];

  struct iovec iov = {
    .iov_base = &c,
    .iov_len = 1,
  };

  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = control,
    .msg_controllen = sizeof(control),
  };
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  *((int *) CMSG_DATA(cmsg)) = memfd;
  assert(sendmsg(fd, &msg, MSG_WAITALL) == 1);
  close(memfd);
}

static
void
vtest_client_run(struct virtio_gpu_dev *dev, int fd) {
  struct vtest_header header = {0};
  assert(recv(fd, &header, sizeof(header), MSG_WAITALL) == sizeof(header));
  assert(header.command == VCMD_CREATE_RENDERER);

  uint32_t ctx_id = 0;

  char debug_name[header.length];
  assert(recv(fd, debug_name, header.length, MSG_WAITALL) == header.length);

  for (;;) {
    assert(recv(fd, &header, sizeof(header), MSG_WAITALL) == sizeof(header));
    switch (header.command) {
    case VCMD_GET_CAPS: {
      struct virtio_gpu_capset_info *info = &dev->capset_infos[0];
      assert(info->max_size > 0);
      size_t size = align_up(info->max_size, 4096);

      void *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, size, -1, 0);
      virtio_gpu_get_capset(dev, 1, info->max_version, info->max_size, buf);

      header.length = info->max_size + 1;
      header.command = 1;

      sendall(fd, &header, sizeof(header));
      sendall(fd, buf, info->max_size);
      vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)buf, size);
      assert(munmap(buf, size) == 0);
      break;
    }
    case VCMD_RESOURCE_UNREF: {
      uint32_t resource_id = 0;
      assert(recv(fd, &resource_id, sizeof(resource_id), MSG_WAITALL) == sizeof(resource_id));

      struct virtio_gpu_resource *rsc = &dev->rscs[resource_id - 1];
      if (rsc->buf)
        vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)rsc->buf, rsc->size);
      virtio_gpu_resource_unref(dev, resource_id);
      break;
    }
    case VCMD_SUBMIT_CMD: {
      init_context(dev, &ctx_id, sizeof(debug_name), debug_name);

      size_t size = header.length * sizeof(uint32_t);
      size_t size2 = align_up(size, 4096);
      uint32_t *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, size2, -1, 0);
      assert(recv(fd, buf, size, MSG_WAITALL) == size);

      virtio_gpu_submit_cmd(dev, ctx_id, size, buf);

      vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)buf, size2);
      assert(munmap(buf, size2) == 0);

      break;
    }
    case VCMD_RESOURCE_BUSY_WAIT: {
      assert(header.length == VCMD_BUSY_WAIT_SIZE);
      struct {
        uint32_t handle;
        uint32_t flags;
      } req;

      assert(recv(fd, &req, sizeof(req), MSG_WAITALL) == sizeof(req));

      uint32_t busy = dev->fence_submitted != dev->fence_completed;

      if (req.flags & VCMD_BUSY_WAIT_FLAG_WAIT) {
        while (busy) {
          virtio_gpu_poll(dev);
          busy = dev->fence_submitted != dev->fence_completed;
        }
      }

      struct {
        struct vtest_header header;
        uint32_t busy;
      } resp = {
        .header = { .length = 1, .command = VCMD_RESOURCE_BUSY_WAIT, },
        .busy = busy,
      };

      sendall(fd, &resp, sizeof(resp));
      break;
    }
    case VCMD_GET_CAPS2: {
      struct virtio_gpu_capset_info *info = &dev->capset_infos[1];
      assert(info->max_size > 0);
      size_t size = align_up(info->max_size, 4096);

      void *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, size, -1, 0);
      virtio_gpu_get_capset(dev, 2, info->max_version, info->max_size, buf);

      header.length = info->max_size + 1;
      header.command = 2;

      sendall(fd, &header, sizeof(header));
      sendall(fd, buf, info->max_size);
      vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)buf, size);
      assert(munmap(buf, size) == 0);
      break;
    };
    case VCMD_PING_PROTOCOL_VERSION: {
      assert(header.length == VCMD_PING_PROTOCOL_VERSION_SIZE);
      sendall(fd, &header, sizeof(header));
      break;
    }
    case VCMD_PROTOCOL_VERSION: {
      assert(header.length == 1);
      uint32_t version;
      assert(recv(fd, &version, sizeof(version), MSG_WAITALL) == sizeof(version));
      assert(version>=3);

      struct {
        struct vtest_header header;
        uint32_t version;
      } resp = {
        .header = {
          .length = VCMD_PROTOCOL_VERSION_SIZE,
          .command = VCMD_PROTOCOL_VERSION,
        },
        .version = 3,
      };

      sendall(fd, &resp, sizeof(resp));
      break;
    }
    case VCMD_RESOURCE_CREATE2: {
      init_context(dev, &ctx_id, sizeof(debug_name), debug_name);

      struct {
        uint32_t res_handle;
        uint32_t target;
        uint32_t format;
        uint32_t bind;
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t array_size;
        uint32_t last_level;
        uint32_t nr_samples;
        uint32_t data_size;
      } req;

      assert(recv(fd, &req, sizeof(req), MSG_WAITALL) == sizeof(req));
      assert(req.res_handle == 0);

      struct virtio_gpu_resource_create_3d_args args = {
        .target = req.target,
        .format = req.format,
        .bind = req.bind,
        .width = req.width,
        .height = req.height,
        .depth = req.depth,
        .array_size = req.array_size,
        .last_level = req.last_level,
        .nr_samples = req.nr_samples,
        .flags = 0,
      };

      uint32_t resource_id = virtio_gpu_resource_create_3d(dev, &args);
      virtio_gpu_ctx_attach_resource(dev, ctx_id, resource_id);

      struct {
        struct vtest_header header;
        uint32_t resource_id;
      } resp = {
        .header = { .length = 1, .command = VCMD_RESOURCE_CREATE2, },
        .resource_id = resource_id,
      };

      sendall(fd, &resp, sizeof(resp));
      if (req.data_size > 0) {
        size_t size = align_up(req.data_size, 4096);
        int memfd = memfd_create("virgl_resource", MFD_CLOEXEC);
        assert(memfd >= 0);
        assert(ftruncate(memfd, size) == 0);
        void *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, size, memfd, 0);
        virtio_gpu_resource_attach_backing(dev, resource_id, buf, size);

        send_memfd(fd, memfd);
      }

      break;
    }
    case VCMD_TRANSFER_GET2: {
      init_context(dev, &ctx_id, sizeof(debug_name), debug_name);

      struct {
        uint32_t res_handle;
        uint32_t level;
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t data_size;
        uint32_t offset;
      } req;

      assert(recv(fd, &req, sizeof(req), MSG_WAITALL) == sizeof(req));

      struct virtio_gpu_transfer_host_3d_args args = {
        .x = req.x,
        .y = req.y,
        .z = req.z,
        .width = req.width,
        .height = req.height,
        .depth = req.depth,
        .offset = req.offset,
        .resource_id = req.res_handle,
        .level = req.level,
        .stride = 0,
        .layer_stride = 0,
      };

      virtio_gpu_transfer_from_host_3d(dev, ctx_id, &args);
      break;
    }
    default:
      fprintf(stderr, "unknown command: %u\n", header.command);
      assert(0);
    }
  }
}

int
main(void) {
  struct vfio_pci_dev pci = {0};
  char const *devid = getenv("DEVID");
  if (devid == NULL)
    devid = "0000:00:05.0";

  vfio_pci_dev_open(devid, &pci);
  vfio_pci_dev_init(&pci);

  struct virtio_gpu_dev dev = {0};
  virtio_gpu_dev_init(&dev, &pci);


  int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(listen_fd >= 0);

  unlink(VTEST_DEFAULT_SOCKET_NAME);

  struct sockaddr_un addr = { .sun_family = AF_UNIX };
  strcpy(addr.sun_path, VTEST_DEFAULT_SOCKET_NAME);
  assert(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  assert(chmod(VTEST_DEFAULT_SOCKET_NAME, 0777) == 0);
  assert(listen(listen_fd, 1) == 0);

  int client_fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
  assert(client_fd >= 0);

  close(listen_fd);
  unlink(VTEST_DEFAULT_SOCKET_NAME);

  vtest_client_run(&dev, client_fd);

  return 0;
}
