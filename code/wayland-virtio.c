#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <wayland-server-core.h>

#include "generated/xdg-shell-server-protocol.h"

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
void
client_destroyed(struct wl_listener *listener, void *data) {
  exit(0);
}

struct buffer_info {
  uint32_t resource_id;
  int32_t width;
  int32_t height;
};

struct compositor_info {
  struct wl_resource *surface;
  size_t callbacks_size;
  size_t callbacks_cap;
  struct wl_resource **callbacks;
  struct wl_resource *buffer;
  uint32_t fence_id;
  int write_fence;
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

  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct buffer_info *info = wl_resource_get_user_data(buffer);

  assert(x == 0);
  assert(y == 0);

  virtio_gpu_set_scanout(dev, info->resource_id, info->width, info->height);
  compositor_info->buffer = buffer;
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
wl_surface_on_commit(struct wl_client *client, struct wl_resource *resource) {
  struct compositor_info *compositor_info = wl_resource_get_user_data(resource);
  if (compositor_info->buffer == NULL)
    return;

  struct buffer_info *info = wl_resource_get_user_data(compositor_info->buffer);

  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  virtio_gpu_transfer_to_host_2d(dev, info->resource_id, info->width, info->height);
  compositor_info->fence_id = virtio_gpu_resource_flush(dev, info->resource_id, info->width, info->height);
  compositor_info->write_fence = 1;
}

static
void
wl_surface_on_damage_buffer(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct buffer_info *info = wl_resource_get_user_data(resource);
  virtio_gpu_transfer_to_host_2d(dev, info->resource_id, info->width, info->height);
}

static
struct wl_surface_interface wl_surface_impl = {
  .destroy = NULL,
  .attach = wl_surface_on_attach,
  .damage = NULL,
  .frame = wl_surface_on_frame,
  .set_opaque_region = NULL,
  .set_input_region = NULL,
  .commit = wl_surface_on_commit,
  .set_buffer_transform = NULL,
  .set_buffer_scale = NULL,
  .damage_buffer = wl_surface_on_damage_buffer,
};

static
void
wl_compositor_on_create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct compositor_info *info = wl_resource_get_user_data(resource);
  assert(info->surface == NULL);

  struct wl_resource *wl_surface = wl_resource_create(client, &wl_surface_interface, 7, id);
  assert(wl_surface != NULL);
  info->surface = wl_surface;
  wl_resource_set_implementation(wl_surface, &wl_surface_impl, info, NULL);
}

static
struct wl_compositor_interface wl_compositor_impl = {
  .create_surface = wl_compositor_on_create_surface,
  .create_region  = NULL,
};

static
struct wl_buffer_interface wl_buffer_impl = {
  .destroy = NULL,
};

struct shm_info {
  char *buf;
  size_t len;
};

static
void
wl_shm_pool_on_create_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id, int32_t offset, int32_t width, int32_t height, int32_t stride, uint32_t format) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct shm_info *shm_info = wl_resource_get_user_data(resource);

  assert(stride == width * 4);

  uint32_t resource_id = virtio_gpu_resource_create_2d(dev, width, height);
  virtio_gpu_resource_attach_backing(dev, resource_id, shm_info->buf + offset, stride * height);

  struct buffer_info *buffer_info = malloc(sizeof(struct buffer_info));
  assert(buffer_info != NULL);
  buffer_info->resource_id = resource_id;
  buffer_info->width = width;
  buffer_info->height = height;

  struct wl_resource *wl_buffer = wl_resource_create(client, &wl_buffer_interface, 1, id);
  assert(wl_buffer != NULL);

  wl_resource_set_implementation(wl_buffer, &wl_buffer_impl, buffer_info, NULL);
}

static
void
wl_shm_pool_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);
  struct shm_info *info = wl_resource_get_user_data(resource);
  vfio_pci_dev_unmap_dma(dev->virtio.pci, (__u64)info->buf, info->len);
  free(info);
}

static
struct wl_shm_pool_interface wl_shm_pool_impl = {
  .create_buffer = wl_shm_pool_on_create_buffer,
  .destroy = wl_shm_pool_on_destroy,
  .resize = NULL,
};

static
void
wl_shm_on_create_pool(struct wl_client *client, struct wl_resource *resource, uint32_t id, int32_t fd, int32_t size) {
  struct virtio_gpu_dev *dev = wl_client_get_user_data(client);

  size_t len = align_up(size, 4096);
  char *buf = vfio_pci_dev_map_dma(dev->virtio.pci, NULL, len, fd, 0);
  assert(buf != NULL);
  close(fd);

  struct shm_info *info = malloc(sizeof(struct shm_info));
  assert(info != NULL);
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
struct xdg_toplevel_interface xdg_toplevel_impl = {
  .destroy = NULL,
  .set_parent = NULL,
  .set_title = NULL,
  .set_app_id = NULL,
  .show_window_menu = NULL,
  .move = NULL,
  .resize = NULL,
  .set_max_size = NULL,
  .set_min_size = NULL,
  .set_maximized = NULL,
  .unset_maximized = NULL,
  .set_fullscreen = NULL,
  .unset_fullscreen = NULL,
  .set_minimized = NULL,
};


static
void
xdg_surface_on_get_top_level(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct wl_resource *xdg_toplevel = wl_resource_create(client, &xdg_toplevel_interface, 7, id);
  assert(xdg_toplevel != NULL);
  wl_resource_set_implementation(xdg_toplevel, &xdg_toplevel_impl, NULL, NULL);
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
  .set_window_geometry = NULL,
  .ack_configure = xdg_surface_on_ack_configure,
};


static
void
xdg_wm_base_on_get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *wl_surface) {
  struct wl_resource *xdg_surface = wl_resource_create(client, &xdg_surface_interface, 7, id);
  assert(xdg_surface != NULL);

  wl_resource_set_implementation(xdg_surface, &xdg_surface_impl, NULL, NULL);

  struct wl_display *display = wl_client_get_display(client);
  uint32_t serial = wl_display_next_serial(display);
  xdg_surface_send_configure(xdg_surface, serial);
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
  wl_resource_set_implementation(resource, &xdg_wm_base_impl, NULL, NULL);
}

static
int
handle_virtio_events(int fd, uint32_t mask, void *data) {
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


int
main(void) {
  struct compositor_info compositor_info = {0};
  struct virtio_gpu_callbacks callbacks = { .write_fence = write_fence };

  struct vfio_pci_dev pci = {0};
  char const *devid = getenv("DEVID");
  if (devid == NULL)
    devid = "0000:00:05.0";

  vfio_pci_dev_open(devid, &pci);
  vfio_pci_dev_init(&pci);

  struct virtio_gpu_dev dev = {0};
  virtio_gpu_dev_init(&dev, &pci, &compositor_info, &callbacks);


  struct wl_display *display = wl_display_create();
  assert(display != NULL);

  wl_global_create(display, &wl_compositor_interface, 6, &compositor_info, wl_compositor_on_bind);
  wl_global_create(display, &wl_shm_interface, 2, NULL, wl_shm_on_bind);
  wl_global_create(display, &xdg_wm_base_interface, 7, NULL, xdg_wm_base_on_bind);

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
  assert(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  assert(listen(listen_fd, 1) == 0);

  int client_fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
  assert(client_fd >= 0);

  close(listen_fd);
  unlink("wayland-0");
  close(lock_fd);

  struct wl_client *client = wl_client_create(display, client_fd);
  assert(client != NULL);

  wl_client_set_user_data(client, &dev, NULL);

  struct wl_listener listener = { .notify = client_destroyed };
  wl_client_add_destroy_listener(client, &listener);

  struct wl_event_loop *loop = wl_display_get_event_loop(display);

  wl_event_loop_add_fd(loop, pci.epollfd, WL_EVENT_READABLE, handle_virtio_events, &dev);

  for (;;) {
    wl_event_loop_dispatch(loop, -1);
    wl_display_flush_clients(display);
  }

  return 0;
}
