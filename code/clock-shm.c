#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h>

#include "generated/xdg-shell-protocol.h"
#include "draw-clock.h"

struct globals {
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct xdg_wm_base *xdg_wm_base;
};

static
void
registry_global(void *data, struct wl_registry *reg, uint32_t id, const char *interface, uint32_t version) {
  struct globals *globals = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    globals->compositor = wl_registry_bind(reg, id, &wl_compositor_interface, version);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    globals->shm = wl_registry_bind(reg, id, &wl_shm_interface, version);
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    globals->xdg_wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, version);
  }
}

static
void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static
void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
  xdg_surface_ack_configure(xdg_surface, serial);
}

struct frame_context {
  struct wl_surface *wl_surface;
  struct wl_buffer *wl_buffer;
  cairo_surface_t *cr_surface;
};

static
struct wl_callback_listener frame_callback_listener;

static
void
request_frame(struct frame_context *context) {
  struct wl_callback *cb = wl_surface_frame(context->wl_surface);
  wl_callback_add_listener(cb, &frame_callback_listener, context);

  int width = cairo_image_surface_get_width(context->cr_surface);
  int height = cairo_image_surface_get_height(context->cr_surface);

  struct wl_callback *frame_cb = wl_surface_frame(context->wl_surface);
  draw_clock(context->cr_surface);
  wl_surface_attach(context->wl_surface, context->wl_buffer, 0, 0);
  wl_surface_damage(context->wl_surface, 0, 0, width, height);
  wl_surface_commit(context->wl_surface);
}

static
void
frame_callback_handler(void *data, struct wl_callback *cb, uint32_t time) {
  wl_callback_destroy(cb);
  struct frame_context *context = data;
  request_frame(context);
}

static
struct wl_callback_listener frame_callback_listener = {
  .done = frame_callback_handler
};

int
main(void) {
  struct wl_display *display = wl_display_connect(NULL);
  assert(display != NULL);

  struct globals globals = {0};

  struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = NULL,
  };

  struct wl_registry *registry = wl_display_get_registry(display);
  assert(registry != NULL);
  assert(wl_registry_add_listener(registry, &registry_listener, &globals) == 0);

  assert(wl_display_roundtrip(display) > 0);

  assert(globals.compositor != NULL);
  assert(globals.shm != NULL);
  assert(globals.xdg_wm_base != NULL);

  struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
  };

  assert(xdg_wm_base_add_listener(globals.xdg_wm_base, &xdg_wm_base_listener, NULL) == 0);
  assert(wl_display_roundtrip(display) > 0);

  struct wl_surface *wl_surface = wl_compositor_create_surface(globals.compositor);
  assert(wl_surface != NULL);

  struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(globals.xdg_wm_base, wl_surface);
  assert(xdg_surface != NULL);

  struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
  };

  assert(xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL) == 0);

  struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
  assert(xdg_toplevel != NULL);

  size_t width = 400;
  size_t height = 400;
  size_t stride = width * 4;
  size_t size = stride * height;

  int fd = memfd_create("shm", MFD_CLOEXEC);
  assert(fd >= 0);
  assert(ftruncate(fd, size) == 0);

  void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  assert(buf != MAP_FAILED);

  struct wl_shm_pool *wl_shm_pool = wl_shm_create_pool(globals.shm, fd, size);
  assert(wl_shm_pool != NULL);
  struct wl_buffer *wl_buffer = wl_shm_pool_create_buffer(wl_shm_pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
  assert(wl_buffer != NULL);
  wl_shm_pool_destroy(wl_shm_pool);
  close(fd);

  cairo_surface_t *cr_surface = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32, width, height, stride);
  assert(cr_surface != NULL);

  struct frame_context context = {
    .wl_surface = wl_surface,
    .wl_buffer = wl_buffer,
    .cr_surface = cr_surface,
  };

  request_frame(&context);

  for (;;)
    wl_display_dispatch(display);

  return 0;
}
