#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>

#include "generated/xdg-shell-server-protocol.h"

#include "virtio-gpu.h"
#include "virtio-input.h"
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


static
void
client_destroyed(struct wl_listener *listener, void *data) {
  exit(0);
}

struct shm_info {
  size_t refcount;
  int fd;
  char *buf;
  size_t len;
};

struct buffer_info {
  struct shm_info *shm;
  uint32_t resource_id;
  int32_t width;
  int32_t height;
  int32_t offset;
};

struct compositor_info {
  struct wl_resource *surface;
  size_t callbacks_size;
  size_t callbacks_cap;
  struct wl_resource **callbacks;
  struct wl_resource *buffer;
  uint32_t fence_id;
  int write_fence;
  uint32_t width;
  uint32_t height;
  uint32_t x;
  uint32_t y;
  struct wl_resource *keyboard;
  struct wl_resource *pointer;
};


static
void
add_callback(struct compositor_info *info, struct wl_resource *callback) {
  if (info->callbacks_size == info->callbacks_cap) {
    info->callbacks_cap = info->callbacks_cap * 2;
    if (info->callbacks_cap == 0)
      info->callbacks_cap = 4;

    struct wl_resource **callbacks = realloc(info->callbacks, sizeof(struct wl_resource *) * info->callbacks_cap);
    assert(callbacks != NULL);
    info->callbacks = callbacks;
  }

  info->callbacks[info->callbacks_size] = callback;
  info->callbacks_size += 1;
}

static
void
write_fence(void *cookie, uint32_t fence_id) {
  struct compositor_info *info = cookie;
  if (info->write_fence == 0)
    return;

  if (fence_id != info->fence_id)
    return;

  wl_buffer_send_release(info->buffer);
  info->buffer = NULL;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint32_t serial = ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));

  for (size_t i=0; i<info->callbacks_size; ++i)
    wl_callback_send_done(info->callbacks[i], serial);

  info->callbacks_size = 0;
  info->write_fence = 0;
}

static
void
wl_surface_on_attach(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer, int32_t x, int32_t y) {
  struct compositor_info *compositor_info = wl_resource_get_user_data(resource);
  if (resource != compositor_info->surface)
    return;

  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct buffer_info *info = wl_resource_get_user_data(buffer);

  assert(x == 0);
  assert(y == 0);

  virtio_gpu_set_scanout(dev, info->resource_id, info->width, info->height);
  compositor_info->buffer = buffer;
}

static
void
wl_surface_on_damage(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct compositor_info *compositor_info = wl_resource_get_user_data(resource);

  if (compositor_info->buffer == NULL)
    return;

  struct buffer_info *info = wl_resource_get_user_data(compositor_info->buffer);

  virtio_gpu_resource_attach_backing(dev, info->resource_id, info->shm->buf + info->offset, info->width * info->height * 4);
  virtio_gpu_transfer_to_host_2d(dev, info->resource_id, info->width, info->height);
  virtio_gpu_resource_detach_backing(dev, info->resource_id);
}

static
void
wl_surface_on_frame(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct compositor_info *info = wl_resource_get_user_data(resource);

  struct wl_resource *wl_callback = wl_resource_create(client, &wl_callback_interface, 1, id);
  assert(wl_callback != NULL);

  add_callback(info, wl_callback);
}

static
void
wl_surface_on_set_opaque_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region) {
}

static
void
wl_surface_on_set_input_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region) {
}

static
void
wl_surface_on_commit(struct wl_client *client, struct wl_resource *resource) {
  struct compositor_info *compositor_info = wl_resource_get_user_data(resource);
  if (compositor_info->buffer == NULL)
    return;

  struct buffer_info *info = wl_resource_get_user_data(compositor_info->buffer);

  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  /* virtio_gpu_transfer_to_host_2d(dev, info->resource_id, info->width, info->height); */
  compositor_info->fence_id = virtio_gpu_resource_flush(dev, info->resource_id, info->width, info->height);
  compositor_info->write_fence = 1;
}

static
void
wl_surface_on_damage_buffer(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct compositor_info *compositor_info = wl_resource_get_user_data(resource);

  if (compositor_info->buffer == NULL)
    return;

  struct buffer_info *info = wl_resource_get_user_data(compositor_info->buffer);

  virtio_gpu_resource_attach_backing(dev, info->resource_id, info->shm->buf + info->offset, info->width * info->height * 4);
  virtio_gpu_transfer_to_host_2d(dev, info->resource_id, info->width, info->height);
  virtio_gpu_resource_detach_backing(dev, info->resource_id);
}

static
struct wl_surface_interface wl_surface_impl = {
  .destroy = NULL,
  .attach = wl_surface_on_attach,
  .damage = wl_surface_on_damage,
  .frame = wl_surface_on_frame,
  .set_opaque_region = wl_surface_on_set_opaque_region,
  .set_input_region = wl_surface_on_set_input_region,
  .commit = wl_surface_on_commit,
  .set_buffer_transform = NULL,
  .set_buffer_scale = NULL,
  .damage_buffer = wl_surface_on_damage_buffer,
};

static
void
wl_compositor_on_create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct compositor_info *info = wl_resource_get_user_data(resource);
  struct wl_resource *wl_surface = wl_resource_create(client, &wl_surface_interface, 7, id);
  assert(wl_surface != NULL);
  wl_resource_set_implementation(wl_surface, &wl_surface_impl, info, NULL);
}


static
void
wl_region_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static
void
wl_region_on_add(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
}


static
struct wl_region_interface wl_region_impl = {
  .destroy = wl_region_on_destroy,
  .add = wl_region_on_add,
  .subtract = NULL,
};

static
void
wl_compositor_on_create_region(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct wl_resource *wl_region = wl_resource_create(client, &wl_region_interface, 7, id);
  assert(wl_region != NULL);
  wl_resource_set_implementation(wl_region, &wl_region_impl, NULL, NULL);
}

static
struct wl_compositor_interface wl_compositor_impl = {
  .create_surface = wl_compositor_on_create_surface,
  .create_region  = wl_compositor_on_create_region,
};

static
void
wl_buffer_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct buffer_info *info = wl_resource_get_user_data(resource);
  info->shm->refcount -= 1;
  if (info->shm->refcount == 0) {
    vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)info->shm->buf, info->shm->len);
    close(info->shm->fd);
    free(info->shm);
  }

  free(info);
  wl_resource_destroy(resource);
}

static
struct wl_buffer_interface wl_buffer_impl = {
  .destroy = wl_buffer_on_destroy,
};

static
void
wl_shm_pool_on_create_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id, int32_t offset, int32_t width, int32_t height, int32_t stride, uint32_t format) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct shm_info *shm_info = wl_resource_get_user_data(resource);

  assert(stride == width * 4);

  uint32_t resource_id = virtio_gpu_resource_create_2d(dev, width, height);

  struct buffer_info *buffer_info = malloc(sizeof(struct buffer_info));
  assert(buffer_info != NULL);
  shm_info->refcount += 1;
  buffer_info->shm = shm_info;
  buffer_info->resource_id = resource_id;
  buffer_info->width = width;
  buffer_info->height = height;
  buffer_info->offset = offset;

  struct wl_resource *wl_buffer = wl_resource_create(client, &wl_buffer_interface, 1, id);
  assert(wl_buffer != NULL);

  wl_resource_set_implementation(wl_buffer, &wl_buffer_impl, buffer_info, NULL);
}

static
void
wl_shm_pool_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct shm_info *info = wl_resource_get_user_data(resource);
  info->refcount -= 1;
  if (info->refcount == 0) {
    vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)info->buf, info->len);
    close(info->fd);
    free(info);
  }
  wl_resource_destroy(resource);
}

static
void
wl_shm_pool_on_resize(struct wl_client *client, struct wl_resource *resource, int32_t size) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct shm_info *info = wl_resource_get_user_data(resource);
  vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)info->buf, info->len);


  size = align_up(size, 4096);
  char *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, size, info->fd, 0);
  assert(buf != NULL);
  info->buf = buf;
  info->len = size;
}

static
struct wl_shm_pool_interface wl_shm_pool_impl = {
  .create_buffer = wl_shm_pool_on_create_buffer,
  .destroy = wl_shm_pool_on_destroy,
  .resize = wl_shm_pool_on_resize,
};

static
void
wl_shm_on_create_pool(struct wl_client *client, struct wl_resource *resource, uint32_t id, int32_t fd, int32_t size) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);

  size_t len = align_up(size, 4096);
  char *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, len, fd, 0);
  assert(buf != NULL);

  struct shm_info *info = malloc(sizeof(struct shm_info));
  assert(info != NULL);
  info->refcount = 1;
  info->fd = fd;
  info->buf = buf;
  info->len = len;

  struct wl_resource *wl_shm_pool = wl_resource_create(client, &wl_shm_pool_interface, 2, id);
  assert(wl_shm_pool != NULL);

  wl_resource_set_implementation(wl_shm_pool, &wl_shm_pool_impl, info, NULL);
}

static
struct wl_shm_interface wl_shm_impl = {
  .create_pool = wl_shm_on_create_pool,
};


static
void
wl_compositor_on_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct wl_resource *resource = wl_resource_create(client, &wl_compositor_interface, version, id);
  assert(resource != NULL);
  wl_resource_set_implementation(resource, &wl_compositor_impl, data, NULL);
}

static
void
wl_shm_on_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct wl_resource *resource = wl_resource_create(client, &wl_shm_interface, version, id);
  assert(resource != NULL);
  wl_resource_set_implementation(resource, &wl_shm_impl, NULL, NULL);
  wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
  wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
}


static
void
xdg_toplevel_on_set_title(struct wl_client *client, struct wl_resource *resource, const char *title) {
}

static
void
xdg_toplevel_on_unset_fullscreen(struct wl_client *client, struct wl_resource *resource) {
}

static
struct xdg_toplevel_interface xdg_toplevel_impl = {
  .destroy = NULL,
  .set_parent = NULL,
  .set_title = xdg_toplevel_on_set_title,
  .set_app_id = NULL,
  .show_window_menu = NULL,
  .move = NULL,
  .resize = NULL,
  .set_max_size = NULL,
  .set_min_size = NULL,
  .set_maximized = NULL,
  .unset_maximized = NULL,
  .set_fullscreen = NULL,
  .unset_fullscreen = xdg_toplevel_on_unset_fullscreen,
  .set_minimized = NULL,
};


static
void
xdg_surface_on_get_top_level(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct compositor_info *info = wl_resource_get_user_data(resource);

  struct wl_resource *xdg_toplevel = wl_resource_create(client, &xdg_toplevel_interface, 7, id);
  assert(xdg_toplevel != NULL);
  wl_resource_set_implementation(xdg_toplevel, &xdg_toplevel_impl, NULL, NULL);

  struct wl_array arr;
  wl_array_init(&arr);

  xdg_toplevel_send_configure(xdg_toplevel, info->width, info->height, &arr);
  wl_array_release(&arr);
}

static
void
xdg_surface_on_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
}

static
void
xdg_surface_on_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
}

static
struct xdg_surface_interface xdg_surface_impl = {
  .destroy = NULL,
  .get_toplevel = xdg_surface_on_get_top_level,
  .get_popup = NULL,
  .set_window_geometry = xdg_surface_on_set_window_geometry,
  .ack_configure = xdg_surface_on_ack_configure,
};


static
void
xdg_wm_base_on_get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *wl_surface) {
  struct compositor_info *info = wl_resource_get_user_data(resource);
  assert(info->surface == NULL);
  info->surface = wl_surface;

  struct wl_resource *xdg_surface = wl_resource_create(client, &xdg_surface_interface, 7, id);
  assert(xdg_surface != NULL);

  wl_resource_set_implementation(xdg_surface, &xdg_surface_impl, info, NULL);

  struct wl_display *display = wl_client_get_display(client);
  uint32_t serial = wl_display_next_serial(display);
  xdg_surface_send_configure(xdg_surface, serial);

  if (info->keyboard) {
    struct wl_display *display = wl_client_get_display(client);
    uint32_t serial = wl_display_next_serial(display);
    struct wl_array arr;
    wl_array_init(&arr);
    wl_keyboard_send_enter(info->keyboard, serial, wl_surface, &arr);
    wl_array_release(&arr);
  }

  if (info->pointer) {
    struct wl_display *display = wl_client_get_display(client);
    uint32_t serial = wl_display_next_serial(display);
    wl_pointer_send_enter(info->pointer, serial, wl_surface, info->x, info->y);
  }
}


static
struct xdg_wm_base_interface xdg_wm_base_impl = {
  .destroy            = NULL,
  .create_positioner  = NULL,
  .get_xdg_surface    = xdg_wm_base_on_get_xdg_surface,
  .pong               = NULL,
};

static
void
xdg_wm_base_on_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct wl_resource *resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
  wl_resource_set_implementation(resource, &xdg_wm_base_impl, data, NULL);
}


static
void
wl_pointer_on_set_cursor(struct wl_client *client, struct wl_resource *resource, uint32_t serial, struct wl_resource *surface, int32_t hotspot_x, int32_t hotspot_y) {
}

static
void
wl_pointer_on_release(struct wl_client *client, struct wl_resource *resource) {
}

static
struct wl_pointer_interface wl_pointer_impl = {
  .set_cursor = wl_pointer_on_set_cursor,
  .release = wl_pointer_on_release,
};

static
void
wl_seat_on_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct compositor_info *info = wl_resource_get_user_data(resource);

  struct wl_resource *wl_pointer = wl_resource_create(client, &wl_pointer_interface, 10, id);
  assert(wl_pointer != NULL);
  wl_resource_set_implementation(wl_pointer, &wl_pointer_impl, info, NULL);

  info->pointer = wl_pointer;

  if (info->surface) {
    struct wl_display *display = wl_client_get_display(client);
    uint32_t serial = wl_display_next_serial(display);
    wl_pointer_send_enter(wl_pointer, serial, info->surface, 0, 0);
  }
}

static
void
wl_keyboard_on_release(struct wl_client *client, struct wl_resource *resource) {
}


static
struct wl_keyboard_interface wl_keyboard_impl = {
  .release = wl_keyboard_on_release,
};


static
void
wl_seat_on_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct compositor_info *info = wl_resource_get_user_data(resource);

  struct wl_resource *wl_keyboard = wl_resource_create(client, &wl_keyboard_interface, 10, id);
  assert(wl_keyboard != NULL);
  wl_resource_set_implementation(wl_keyboard, &wl_keyboard_impl, info, NULL);

  info->keyboard = wl_keyboard;

  int fd = memfd_create("keymap", MFD_CLOEXEC);
  wl_keyboard_send_keymap(wl_keyboard, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, fd, 0);
  close(fd);

  if (info->surface) {
    struct wl_display *display = wl_client_get_display(client);
    uint32_t serial = wl_display_next_serial(display);
    struct wl_array arr;
    wl_array_init(&arr);
    wl_keyboard_send_enter(wl_keyboard, serial, info->surface, &arr);
    wl_array_release(&arr);
  }
}

static
struct wl_seat_interface wl_seat_impl = {
  .get_pointer = wl_seat_on_get_pointer,
  .get_keyboard = wl_seat_on_get_keyboard,
  .get_touch = NULL,
  .release = NULL,
};

static
void
wl_seat_on_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct wl_resource *resource = wl_resource_create(client, &wl_seat_interface, version, id);
  wl_resource_set_implementation(resource, &wl_seat_impl, data, NULL);
  uint32_t capabilities = WL_SEAT_CAPABILITY_KEYBOARD|WL_SEAT_CAPABILITY_POINTER;
  wl_seat_send_capabilities(resource, capabilities);
}


struct loop_info {
  struct wl_event_loop *loop;
  struct wl_display *display;
  struct compositor_info *info;
  struct virtio_gpu_dev *gpu;
  struct virtio_input_dev *keyboard;
  struct virtio_input_dev *mouse;
};


static
int
handle_gpu_events(int fd, uint32_t mask, void *data) {
  struct virtio_gpu_dev *dev = data;
  int64_t n = vfio_pci_dev_read_event(dev->virtio.pci, 0);
  if (n < 0)
    return 0;

  if (n != 0) {
    vfio_pci_dev_clear_irq(dev->virtio.pci, n);
    return 0;
  }

  virtio_gpu_poll(dev);
  return 0;
}

static
int
handle_keyboard_events(int fd, uint32_t mask, void *data) {
  struct loop_info *info = data;
  struct virtio_input_dev *dev = info->keyboard;
  int64_t n = vfio_pci_dev_read_event(dev->virtio.pci, 0);
  if (n < 0)
    return 0;

  vfio_pci_dev_clear_irq(dev->virtio.pci, n);

  if (n != 0)
    return 0;

  virtio_queue_handle_event(&dev->virtio.queues[0]);

  struct virtio_input_event event = {0};

  while (virtio_input_recv(dev, &event)) {
    switch(event.type) {
    case EV_KEY: {
      if (info->info->surface && info->info->keyboard) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t time = ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
        uint32_t serial = wl_display_next_serial(info->display);

        wl_keyboard_send_key(info->info->keyboard, serial, time, event.code, event.value);
      }
    }
    default:
      break;
    }
  }

  return 0;
}

static
int
handle_mouse_events(int fd, uint32_t mask, void *data) {
  struct loop_info *info = data;
  struct virtio_input_dev *dev = info->mouse;
  int64_t n = vfio_pci_dev_read_event(dev->virtio.pci, 0);
  if (n < 0)
    return 0;

  vfio_pci_dev_clear_irq(dev->virtio.pci, n);

  if (n != 0)
    return 0;

  virtio_queue_handle_event(&dev->virtio.queues[0]);

  struct virtio_input_event event = {0};

  while (virtio_input_recv(dev, &event)) {
    switch(event.type) {
    case EV_SYN: {
      if (info->info->surface && info->info->pointer) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t time = ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));

        wl_pointer_send_motion(info->info->pointer, time, info->info->x, info->info->y);
        wl_pointer_send_frame(info->info->pointer);
      }
      break;
    }
    case EV_KEY: {
      if (info->info->surface && info->info->pointer) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t time = ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
        uint32_t serial = wl_display_next_serial(info->display);

        wl_pointer_send_button(info->info->pointer, serial, time, event.code, event.value);
      }
    }
    case EV_ABS: {
      if (event.code == ABS_X) {
        info->info->x = wl_fixed_from_double((double)event.value * info->info->width / 32767.0);
      } else if (event.code == ABS_Y) {
        info->info->y = wl_fixed_from_double((double)event.value * info->info->height / 32767.0);
      }
      break;
    }
    default:
      break;
    }
  }

  return 0;
}

struct virgl_info {
  struct loop_info *loop;
  uint32_t ctx_id;
  size_t debug_name_size;
  char *debug_name;
};


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
int
handle_virgl_events(int fd, uint32_t mask, void *data) {
  struct virgl_info *info = data;
  struct virtio_gpu_dev *dev = info->loop->gpu;

  struct vtest_header header = {0};
  assert(recv(fd, &header, sizeof(header), MSG_WAITALL) == sizeof(header));

  if (info->debug_name == NULL) {
    assert(header.command == VCMD_CREATE_RENDERER);
    info->debug_name_size = header.length;
    info->debug_name = malloc(header.length);
    assert(recv(fd, info->debug_name, header.length, MSG_WAITALL) == header.length);
    return 0;
  }

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
    init_context(dev, &info->ctx_id, info->debug_name_size, info->debug_name);

    size_t size = header.length * sizeof(uint32_t);
    size_t size2 = align_up(size, 4096);
    uint32_t *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, size2, -1, 0);
    assert(recv(fd, buf, size, MSG_WAITALL) == size);

    virtio_gpu_submit_cmd(dev, info->ctx_id, size, buf);

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
  case VCMD_PING_PROTOCOL_VERSION: {
    assert(header.length == VCMD_PING_PROTOCOL_VERSION_SIZE);
    sendall(fd, &header, sizeof(header));
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
    init_context(dev, &info->ctx_id, info->debug_name_size, info->debug_name);

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
    virtio_gpu_ctx_attach_resource(dev, info->ctx_id, resource_id);

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
    init_context(dev, &info->ctx_id, info->debug_name_size, info->debug_name);

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

    virtio_gpu_transfer_from_host_3d(dev, info->ctx_id, &args);
    break;
  }
  default:
    fprintf(stderr, "unknown command: %u\n", header.command);
    assert(0);
  }
}

static
int
handle_listen_events(int fd, uint32_t mask, void *data) {
  struct loop_info *loop = data;
  for (;;) {
    int client_fd = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
    if (client_fd < 0)
      break;


    struct virgl_info *info = malloc(sizeof(struct virgl_info));
    info->loop = loop;
    info->ctx_id = 0;
    info->debug_name = NULL;
    wl_event_loop_add_fd(loop->loop, client_fd, WL_EVENT_READABLE, handle_virgl_events, info);
  }
  return 0;
}


int
main(void) {
  struct compositor_info compositor_info = {0};
  struct virtio_gpu_callbacks callbacks = { .write_fence = write_fence };

  struct vfio_pci_dev gpu_pci = {0};
  char const *gpu_devid = getenv("GPU_DEVID");
  if (gpu_devid == NULL)
    gpu_devid = "0000:00:05.0";

  vfio_pci_dev_open(gpu_devid, &gpu_pci);
  vfio_pci_dev_init(&gpu_pci);

  struct virtio_gpu_dev gpu_dev = {0};
  virtio_gpu_dev_init(&gpu_dev, &gpu_pci, &compositor_info, &callbacks);

  struct virtio_gpu_resp_display_info display_info = virtio_gpu_get_display_info(&gpu_dev);
  assert(display_info.pmodes[0].r.x == 0);
  assert(display_info.pmodes[0].r.y == 0);

  compositor_info.width = display_info.pmodes[0].r.width;
  compositor_info.height = display_info.pmodes[0].r.height;


  struct vfio_pci_dev keyboard_pci = {0};
  char const *keyboard_devid = getenv("KEYBOARD_DEVID");
  if (keyboard_devid == NULL)
    keyboard_devid = "0000:00:08.0";

  vfio_pci_dev_open(keyboard_devid, &keyboard_pci);
  vfio_pci_dev_init(&keyboard_pci);

  struct virtio_input_dev keyboard_dev = {0};
  virtio_input_dev_init(&keyboard_dev, &keyboard_pci);


  struct vfio_pci_dev mouse_pci = {0};
  char const *mouse_devid = getenv("MOUSE_DEVID");
  if (mouse_devid == NULL)
    mouse_devid = "0000:00:09.0";

  vfio_pci_dev_open(mouse_devid, &mouse_pci);
  vfio_pci_dev_init(&mouse_pci);

  struct virtio_input_dev mouse_dev = {0};
  virtio_input_dev_init(&mouse_dev, &mouse_pci);


  struct wl_display *display = wl_display_create();
  assert(display != NULL);

  wl_global_create(display, &wl_compositor_interface, 6, &compositor_info, wl_compositor_on_bind);
  wl_global_create(display, &wl_shm_interface, 2, NULL, wl_shm_on_bind);
  wl_global_create(display, &xdg_wm_base_interface, 7, &compositor_info, xdg_wm_base_on_bind);
  wl_global_create(display, &wl_seat_interface, 10, &compositor_info, wl_seat_on_bind);

  const char *dir = getenv("XDG_RUNTIME_DIR");
  assert(dir);
  assert(chdir(dir) == 0);
  int lock_fd = open("wayland-0.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0640);
  assert(lock_fd >= 0);
  assert(flock(lock_fd, LOCK_EX) == 0);

  int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(listen_fd >= 0);

  unlink("wayland-0");

  struct sockaddr_un addr = { .sun_family = AF_UNIX };
  strcpy(addr.sun_path, "wayland-0");
  mode_t old_mask = umask(0);
  assert(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  umask(old_mask);
  assert(listen(listen_fd, 1) == 0);

  int client_fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
  assert(client_fd >= 0);

  close(listen_fd);
  unlink("wayland-0");
  close(lock_fd);

  struct wl_client *client = wl_client_create(display, client_fd);
  assert(client != NULL);

  wl_client_set_user_data(client, &gpu_dev, NULL);

  struct wl_listener listener = { .notify = client_destroyed };
  wl_client_add_destroy_listener(client, &listener);


  listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  assert(listen_fd >= 0);
  unlink(VTEST_DEFAULT_SOCKET_NAME);
  strcpy(addr.sun_path, VTEST_DEFAULT_SOCKET_NAME);
  old_mask = umask(0);
  assert(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  umask(old_mask);
  assert(listen(listen_fd, 1) == 0);


  struct wl_event_loop *loop = wl_display_get_event_loop(display);

  struct loop_info loop_info = {
    .loop = loop,
    .display = display,
    .info = &compositor_info,
    .gpu = &gpu_dev,
    .keyboard = &keyboard_dev,
    .mouse = &mouse_dev,
  };

  wl_event_loop_add_fd(loop, gpu_pci.epollfd, WL_EVENT_READABLE, handle_gpu_events, &gpu_dev);
  wl_event_loop_add_fd(loop, keyboard_pci.epollfd, WL_EVENT_READABLE, handle_keyboard_events, &loop_info);
  wl_event_loop_add_fd(loop, mouse_pci.epollfd, WL_EVENT_READABLE, handle_mouse_events, &loop_info);
  wl_event_loop_add_fd(loop, listen_fd, WL_EVENT_READABLE, handle_listen_events, &loop_info);


  for (;;) {
    wl_event_loop_dispatch(loop, -1);
    wl_display_flush_clients(display);
  }

  return 0;
}
