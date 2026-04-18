#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <wayland-client-core.h>
#include <wayland-server-core.h>

#include "generated/xdg-shell-client-protocol.h"
#include "generated/xdg-shell-server-protocol.h"

static
void
client_destroyed(struct wl_listener *listener, void *data) {
  exit(0);
}

static
void
frame_callback_done(void *data, struct wl_callback *cb, uint32_t time) {
  struct wl_resource *frontend_wl_callback = data;
  wl_callback_send_done(frontend_wl_callback, time);
}

static
struct wl_callback_listener frame_callback_listener = {
  .done = frame_callback_done
};

static
void
wl_surface_on_attach(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer, int32_t x, int32_t y) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  struct wl_buffer *backend_wl_buffer = wl_resource_get_user_data(buffer);
  wl_surface_attach(backend_wl_surface, backend_wl_buffer, x, y);
}

static
void
wl_surface_on_frame(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct wl_resource *wl_callback = wl_resource_create(client, &wl_callback_interface, 1, id);
  assert(wl_callback != NULL);

  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  struct wl_callback *backend_wl_ballback = wl_surface_frame(backend_wl_surface);
  wl_callback_add_listener(backend_wl_ballback, &frame_callback_listener, wl_callback);
}

static
void
wl_surface_on_commit(struct wl_client *client, struct wl_resource *resource) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  wl_surface_commit(backend_wl_surface);
}

static
void
wl_surface_on_damage_buffer(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  wl_surface_damage_buffer(backend_wl_surface, x, y, width, height);
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
  struct wl_compositor *backend_wl_compositor = wl_resource_get_user_data(resource);
  struct wl_surface *backend_wl_surface = wl_compositor_create_surface(backend_wl_compositor);
  struct wl_resource *wl_surface = wl_resource_create(client, &wl_surface_interface, 7, id);
  assert(wl_surface != NULL);
  wl_resource_set_implementation(wl_surface, &wl_surface_impl, backend_wl_surface, NULL);
}

static
struct wl_compositor_interface wl_compositor_impl = {
  .create_surface = wl_compositor_on_create_surface,
  .create_region  = NULL,
};

static
void
wl_compositor_on_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct wl_resource *resource = wl_resource_create(client, &wl_compositor_interface, version, id);
  assert(resource != NULL);
  wl_resource_set_implementation(resource, &wl_compositor_impl, data, NULL);
}

static
struct wl_buffer_interface wl_buffer_impl = {
  .destroy = NULL,
};

static
void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
  struct wl_resource *frontend_buffer = data;
  wl_buffer_send_release(frontend_buffer);
}

static
struct wl_buffer_listener wl_buffer_listener = {
  .release = wl_buffer_release,
};

static
void
wl_shm_pool_on_create_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id, int32_t offset, int32_t width, int32_t height, int32_t stride, uint32_t format) {
  struct wl_shm_pool *backend_wl_shm_pool = wl_resource_get_user_data(resource);
  struct wl_buffer *backend_buffer = wl_shm_pool_create_buffer(backend_wl_shm_pool, offset, width, height, stride, format);

  struct wl_resource *wl_buffer = wl_resource_create(client, &wl_buffer_interface, 1, id);
  assert(wl_buffer != NULL);
  wl_resource_set_implementation(wl_buffer, &wl_buffer_impl, backend_buffer, NULL);

  wl_buffer_add_listener(backend_buffer, &wl_buffer_listener, wl_buffer);
}

static
void
wl_shm_pool_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct wl_shm_pool *backend_wl_shm_pool = wl_resource_get_user_data(resource);
  wl_shm_pool_destroy(backend_wl_shm_pool);
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
  struct wl_shm *backend_wl_shm = wl_resource_get_user_data(resource);
  struct wl_shm_pool *backend_wl_shm_pool = wl_shm_create_pool(backend_wl_shm, fd, size);

  struct wl_resource *wl_shm_pool = wl_resource_create(client, &wl_shm_pool_interface, 2, id);
  assert(wl_shm_pool != NULL);
  wl_resource_set_implementation(wl_shm_pool, &wl_shm_pool_impl, backend_wl_shm_pool, NULL);
}

static
struct wl_shm_interface wl_shm_impl = {
  .create_pool = wl_shm_on_create_pool,
};

static
void
wl_shm_on_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct wl_resource *resource = wl_resource_create(client, &wl_shm_interface, version, id);
  assert(resource != NULL);
  wl_resource_set_implementation(resource, &wl_shm_impl, data, NULL);
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
  struct xdg_surface *backend_xdg_surface = wl_resource_get_user_data(resource);
  struct xdg_toplevel *backend_xdg_toplevel = xdg_surface_get_toplevel(backend_xdg_surface);

  struct wl_resource *xdg_toplevel = wl_resource_create(client, &xdg_toplevel_interface, 7, id);
  assert(xdg_toplevel != NULL);
  wl_resource_set_implementation(xdg_toplevel, &xdg_toplevel_impl, backend_xdg_toplevel, NULL);
}

static
struct xdg_surface_interface xdg_surface_impl = {
  .destroy = NULL,
  .get_toplevel = xdg_surface_on_get_top_level,
  .get_popup = NULL,
  .set_window_geometry = NULL,
  .ack_configure = NULL,
};

static
void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
  xdg_surface_ack_configure(xdg_surface, serial);
}

static
struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_configure,
};

static
void
xdg_wm_base_on_get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *wl_surface) {
  struct xdg_wm_base *backend_xdg_wm_base = wl_resource_get_user_data(resource);
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(wl_surface);
  struct xdg_surface *backend_xdg_surface = xdg_wm_base_get_xdg_surface(backend_xdg_wm_base, backend_wl_surface);

  assert(xdg_surface_add_listener(backend_xdg_surface, &xdg_surface_listener, NULL) == 0);

  struct wl_resource *xdg_surface = wl_resource_create(client, &xdg_surface_interface, 7, id);
  assert(xdg_surface != NULL);
  wl_resource_set_implementation(xdg_surface, &xdg_surface_impl, backend_xdg_surface, NULL);
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
int
handle_backend_events(int fd, uint32_t mask, void *data) {
  struct wl_display *backend_wl_display = data;
  wl_display_dispatch(backend_wl_display);
  wl_display_flush(backend_wl_display);
  return 0;
}

int
main(void) {
  struct wl_display *wl_display = wl_display_connect(NULL);
  assert(wl_display != NULL);

  struct globals globals = {0};

  struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = NULL,
  };

  struct wl_registry *registry = wl_display_get_registry(wl_display);
  assert(registry != NULL);
  assert(wl_registry_add_listener(registry, &registry_listener, &globals) == 0);

  assert(wl_display_roundtrip(wl_display) > 0);

  assert(globals.compositor != NULL);
  assert(globals.shm != NULL);
  assert(globals.xdg_wm_base != NULL);

  struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
  };
  assert(xdg_wm_base_add_listener(globals.xdg_wm_base, &xdg_wm_base_listener, NULL) == 0);
  assert(wl_display_roundtrip(wl_display) > 0);

  struct wl_display *display = wl_display_create();
  assert(display != NULL);

  wl_global_create(display, &wl_compositor_interface, 6, globals.compositor, wl_compositor_on_bind);
  wl_global_create(display, &wl_shm_interface, 2, globals.shm, wl_shm_on_bind);
  wl_global_create(display, &xdg_wm_base_interface, 7, globals.xdg_wm_base, xdg_wm_base_on_bind);

  const char *dir = getenv("XDG_RUNTIME_DIR");
  assert(dir);
  assert(chdir(dir) == 0);
  int lock_fd = open("wayland-1.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0640);
  assert(lock_fd >= 0);
  assert(flock(lock_fd, LOCK_EX) == 0);

  int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(listen_fd >= 0);

  unlink("wayland-1");

  struct sockaddr_un addr = { .sun_family = AF_UNIX };
  strcpy(addr.sun_path, "wayland-1");
  assert(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  assert(listen(listen_fd, 1) == 0);

  int client_fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
  assert(client_fd >= 0);

  close(listen_fd);
  unlink("wayland-1");
  close(lock_fd);

  struct wl_client *client = wl_client_create(display, client_fd);
  assert(client != NULL);

  struct wl_listener listener = { .notify = client_destroyed };
  wl_client_add_destroy_listener(client, &listener);

  struct wl_event_loop *loop = wl_display_get_event_loop(display);
  int backend_fd = wl_display_get_fd(wl_display);

  wl_event_loop_add_fd(
        loop,
        backend_fd,
        WL_EVENT_READABLE,
        handle_backend_events,
        wl_display);

  for (;;) {
    wl_event_loop_dispatch(loop, -1);
    wl_display_flush_clients(display);
    wl_display_flush(wl_display);
  }

  return 0;
}
