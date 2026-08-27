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
wl_surface_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  wl_surface_destroy(backend_wl_surface);
}

static
void
wl_surface_on_attach(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer, int32_t x, int32_t y) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  struct wl_buffer *backend_wl_buffer = wl_resource_get_user_data(buffer);
  wl_surface_attach(backend_wl_surface, backend_wl_buffer, x, y);
}

static
void
wl_surface_on_damage(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  wl_surface_damage(backend_wl_surface, x, y, width, height);
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
wl_surface_on_set_opaque_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  struct wl_region *backend_wl_region = wl_resource_get_user_data(region);
  wl_surface_set_opaque_region(backend_wl_surface, backend_wl_region);
}

static
void
wl_surface_on_set_input_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region) {
  struct wl_surface *backend_wl_surface = wl_resource_get_user_data(resource);
  struct wl_region *backend_wl_region = wl_resource_get_user_data(region);
  wl_surface_set_input_region(backend_wl_surface, backend_wl_region);
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
  .destroy = wl_surface_on_destroy,
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
wl_surface_preferred_buffer_scale(void *data, struct wl_surface *wl_surface, int32_t factor) {
}



static
struct wl_surface_listener wl_surface_listener = {
  .enter = NULL,
  .leave = NULL,
  .preferred_buffer_scale = wl_surface_preferred_buffer_scale,
  .preferred_buffer_transform = NULL,
};

static
void
wl_compositor_on_create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct wl_compositor *backend_wl_compositor = wl_resource_get_user_data(resource);
  struct wl_surface *backend_wl_surface = wl_compositor_create_surface(backend_wl_compositor);

  struct wl_resource *wl_surface = wl_resource_create(client, &wl_surface_interface, 7, id);
  assert(wl_surface != NULL);
  wl_resource_set_implementation(wl_surface, &wl_surface_impl, backend_wl_surface, NULL);
  wl_surface_add_listener(backend_wl_surface, &wl_surface_listener, wl_surface);
}


static
void
wl_region_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct wl_region *wl_region = wl_resource_get_user_data(resource);
  wl_region_destroy(wl_region);
}

static
void
wl_region_on_add(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
  struct wl_region *wl_region = wl_resource_get_user_data(resource);
  wl_region_add(wl_region, x, y, width, height);
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
  struct wl_compositor *backend_wl_compositor = wl_resource_get_user_data(resource);
  struct wl_region *backend_wl_region = wl_compositor_create_region(backend_wl_compositor);
  struct wl_resource *wl_region = wl_resource_create(client, &wl_region_interface, 7, id);
  assert(wl_region != NULL);
  wl_resource_set_implementation(wl_region, &wl_region_impl, backend_wl_region, NULL);
}

static
struct wl_compositor_interface wl_compositor_impl = {
  .create_surface = wl_compositor_on_create_surface,
  .create_region  = wl_compositor_on_create_region,
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
wl_buffer_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct wl_buffer *wl_buffer = wl_resource_get_user_data(resource);
  wl_buffer_destroy(wl_buffer);
}

static
struct wl_buffer_interface wl_buffer_impl = {
  .destroy = wl_buffer_on_destroy,
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
void
wl_shm_pool_on_resize(struct wl_client *client, struct wl_resource *resource, int32_t size) {
  struct wl_shm_pool *backend_wl_shm_pool = wl_resource_get_user_data(resource);
  wl_shm_pool_resize(backend_wl_shm_pool, size);
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
void
xdg_toplevel_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct xdg_toplevel *backend_xdg_toplevel = wl_resource_get_user_data(resource);
  xdg_toplevel_destroy(backend_xdg_toplevel);
}

static
void
xdg_toplevel_on_set_title(struct wl_client *client, struct wl_resource *resource, const char *title) {
  struct xdg_toplevel *backend_xdg_toplevel = wl_resource_get_user_data(resource);
  xdg_toplevel_set_title(backend_xdg_toplevel, title);
}

static
void
xdg_toplevel_on_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial) {
  struct xdg_toplevel *backend_xdg_toplevel = wl_resource_get_user_data(resource);
  struct wl_seat *backend_wl_seat = wl_resource_get_user_data(seat);
  xdg_toplevel_move(backend_xdg_toplevel, backend_wl_seat, serial);
}


static
void
xdg_toplevel_on_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial, uint32_t edges) {
  struct xdg_toplevel *backend_xdg_toplevel = wl_resource_get_user_data(resource);
  struct wl_seat *backend_wl_seat = wl_resource_get_user_data(seat);
  xdg_toplevel_resize(backend_xdg_toplevel, backend_wl_seat, serial, edges);
}


static
void
xdg_toplevel_on_unset_fullscreen(struct wl_client *client, struct wl_resource *resource) {
  struct xdg_toplevel *backend_xdg_toplevel = wl_resource_get_user_data(resource);
  xdg_toplevel_unset_fullscreen(backend_xdg_toplevel);
}

static
struct xdg_toplevel_interface xdg_toplevel_impl = {
  .destroy = xdg_toplevel_on_destroy,
  .set_parent = NULL,
  .set_title = xdg_toplevel_on_set_title,
  .set_app_id = NULL,
  .show_window_menu = NULL,
  .move = xdg_toplevel_on_move,
  .resize = xdg_toplevel_on_resize,
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
xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
  struct wl_resource *frontend_xdg_toplevel = data;
  xdg_toplevel_send_configure(frontend_xdg_toplevel, width, height, states);
}

static
void
xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
}


static
void
xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {
}


struct xdg_toplevel_listener xdg_toplevel_listener = {
  .configure = xdg_toplevel_configure,
  .close = NULL,
  .configure_bounds = xdg_toplevel_configure_bounds,
  .wm_capabilities = xdg_toplevel_wm_capabilities,
};


static
void
xdg_surface_on_destroy(struct wl_client *client, struct wl_resource *resource) {
  struct xdg_surface *backend_xdg_surface = wl_resource_get_user_data(resource);
  xdg_surface_destroy(backend_xdg_surface);
}

static
void
xdg_surface_on_get_top_level(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct xdg_surface *backend_xdg_surface = wl_resource_get_user_data(resource);
  struct xdg_toplevel *backend_xdg_toplevel = xdg_surface_get_toplevel(backend_xdg_surface);

  struct wl_resource *xdg_toplevel = wl_resource_create(client, &xdg_toplevel_interface, 7, id);
  assert(xdg_toplevel != NULL);
  wl_resource_set_implementation(xdg_toplevel, &xdg_toplevel_impl, backend_xdg_toplevel, NULL);

  assert(xdg_toplevel_add_listener(backend_xdg_toplevel, &xdg_toplevel_listener, xdg_toplevel) == 0);
}

static
void
xdg_surface_on_ack_confiigure(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
  struct xdg_surface *backend_xdg_surface = wl_resource_get_user_data(resource);
  xdg_surface_ack_configure(backend_xdg_surface, serial);
}

static
void
xdg_surface_on_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
  struct xdg_surface *backend_xdg_surface = wl_resource_get_user_data(resource);
  xdg_surface_set_window_geometry(backend_xdg_surface, x, y, width, height);
}


static
struct xdg_surface_interface xdg_surface_impl = {
  .destroy = xdg_surface_on_destroy,
  .get_toplevel = xdg_surface_on_get_top_level,
  .get_popup = NULL,
  .set_window_geometry = xdg_surface_on_set_window_geometry,
  .ack_configure = xdg_surface_on_ack_confiigure,
};

static
void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
  struct wl_resource *frontend_xdg_surface = data;
  xdg_surface_send_configure(frontend_xdg_surface, serial);
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

  struct wl_resource *xdg_surface = wl_resource_create(client, &xdg_surface_interface, 7, id);
  assert(xdg_surface != NULL);

  assert(xdg_surface_add_listener(backend_xdg_surface, &xdg_surface_listener, xdg_surface) == 0);
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

static
void
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
  struct wl_resource *frontend_wl_pointer = data;
  struct wl_resource *frontend_wl_surface = wl_surface_get_user_data(surface);
  wl_pointer_send_enter(frontend_wl_pointer, serial, frontend_wl_surface, surface_x, surface_y);
}

static
void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
  struct wl_resource *frontend_wl_pointer = data;
  if (surface == NULL)
    return;
  struct wl_resource *frontend_wl_surface = wl_surface_get_user_data(surface);
  wl_pointer_send_leave(frontend_wl_pointer, serial, frontend_wl_surface);
}

static
void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_motion(frontend_wl_pointer, time, surface_x, surface_y);
}

static
void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer,uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_button(frontend_wl_pointer, serial, time, button, state);
}


static
void
wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_axis(frontend_wl_pointer, time, axis, value);
}

static
void
wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_frame(frontend_wl_pointer);
}

static
void
wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_axis_source(frontend_wl_pointer, axis_source);
}

static
void
wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_axis_stop(frontend_wl_pointer, time, axis);
}

static
void
wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_axis_discrete(frontend_wl_pointer, axis, discrete);
}

static
void
wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_axis_value120(frontend_wl_pointer, axis, value120);
}

static
void
wl_pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction) {
  struct wl_resource *frontend_wl_pointer = data;
  wl_pointer_send_axis_relative_direction(frontend_wl_pointer, axis, direction);
}

static
struct wl_pointer_listener wl_pointer_listener = {
  .enter = wl_pointer_enter,
  .leave = wl_pointer_leave,
  .motion = wl_pointer_motion,
  .button = wl_pointer_button,
  .axis = wl_pointer_axis,
  .frame = wl_pointer_frame,
  .axis_source = wl_pointer_axis_source,
  .axis_stop = wl_pointer_axis_stop,
  .axis_discrete = wl_pointer_axis_discrete,
  .axis_value120 = wl_pointer_axis_value120,
  .axis_relative_direction = wl_pointer_axis_relative_direction,
};

static
void
wl_pointer_on_set_cursor(struct wl_client *client, struct wl_resource *resource, uint32_t serial, struct wl_resource *surface, int32_t hotspot_x, int32_t hotspot_y) {
  struct wl_pointer *backend_wl_pointer = wl_resource_get_user_data(resource);
  struct wl_surface *backend_wl_surface = (surface)?wl_resource_get_user_data(surface):NULL;
  wl_pointer_set_cursor(backend_wl_pointer, serial, backend_wl_surface, hotspot_x, hotspot_y);
}

static
void
wl_pointer_on_release(struct wl_client *client, struct wl_resource *resource) {
  struct wl_pointer *backend_wl_pointer = wl_resource_get_user_data(resource);
  wl_pointer_release(backend_wl_pointer);
}

static
struct wl_pointer_interface wl_pointer_impl = {
  .set_cursor = wl_pointer_on_set_cursor,
  .release = wl_pointer_on_release,
};

static
void
wl_seat_on_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct wl_seat *backend_wl_seat = wl_resource_get_user_data(resource);
  struct wl_pointer *backend_wl_pointer = wl_seat_get_pointer(backend_wl_seat);

  struct wl_resource *wl_pointer = wl_resource_create(client, &wl_pointer_interface, 10, id);
  assert(wl_pointer != NULL);
  wl_resource_set_implementation(wl_pointer, &wl_pointer_impl, backend_wl_pointer, NULL);
  wl_pointer_add_listener(backend_wl_pointer, &wl_pointer_listener, wl_pointer);
}

static
void
wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
  struct wl_resource *frontend_wl_keyboard = data;
  wl_keyboard_send_keymap(frontend_wl_keyboard, format, fd, size);
  close(fd);
}

static
void
wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
  struct wl_resource *frontend_wl_keyboard = data;
  struct wl_resource *frontend_wl_surface = wl_surface_get_user_data(surface);
  wl_keyboard_send_enter(frontend_wl_keyboard, serial, frontend_wl_surface, keys);
}

static
void
wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) {
  struct wl_resource *frontend_wl_keyboard = data;
  if (surface == NULL)
    return;
  struct wl_resource *frontend_wl_surface = wl_surface_get_user_data(surface);
  wl_keyboard_send_leave(frontend_wl_keyboard, serial, frontend_wl_surface);
}

static
void
wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
  struct wl_resource *frontend_wl_keyboard = data;
  wl_keyboard_send_key(frontend_wl_keyboard, serial, time, key, state);
}

static
void
wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
  struct wl_resource *frontend_wl_keyboard = data;
  wl_keyboard_send_modifiers(frontend_wl_keyboard, serial, mods_depressed, mods_latched, mods_locked, group);
}

static
void
wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {
  struct wl_resource *frontend_wl_keyboard = data;
  wl_keyboard_send_repeat_info(frontend_wl_keyboard, rate, delay);
}

static
struct wl_keyboard_listener wl_keyboard_listener = {
  .keymap = wl_keyboard_keymap,
  .enter = wl_keyboard_enter,
  .leave = wl_keyboard_leave,
  .key = wl_keyboard_key,
  .modifiers = wl_keyboard_modifiers,
  .repeat_info = wl_keyboard_repeat_info,
};

static
void
wl_keyboard_on_release(struct wl_client *client, struct wl_resource *resource) {
  struct wl_keyboard *backend_wl_keyboard = wl_resource_get_user_data(resource);
  wl_keyboard_release(backend_wl_keyboard);
}


static
struct wl_keyboard_interface wl_keyboard_impl = {
  .release = wl_keyboard_on_release,
};

static
void
wl_seat_on_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
  struct wl_seat *backend_wl_seat = wl_resource_get_user_data(resource);
  struct wl_keyboard *backend_wl_keyboard = wl_seat_get_keyboard(backend_wl_seat);

  struct wl_resource *wl_keyboard = wl_resource_create(client, &wl_keyboard_interface, 10, id);
  assert(wl_keyboard != NULL);
  wl_resource_set_implementation(wl_keyboard, &wl_keyboard_impl, backend_wl_keyboard, NULL);
  wl_keyboard_add_listener(backend_wl_keyboard, &wl_keyboard_listener, wl_keyboard);
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
  struct wl_seat *wl_seat = data;
  uint32_t *capabilities = wl_seat_get_user_data(wl_seat);
  wl_seat_send_capabilities(resource, *capabilities);
}

struct globals {
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct xdg_wm_base *xdg_wm_base;
  struct wl_seat *seat;
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
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    globals->seat = wl_registry_bind(reg, id, &wl_seat_interface, version);
  }
}

static
void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static
struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = xdg_wm_base_ping,
};

static
void
wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
  uint32_t *p = data;
  *p = capabilities & (WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

static
void
wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
}

static
struct wl_seat_listener wl_seat_listener = {
  .capabilities = wl_seat_capabilities,
  .name = wl_seat_name,
};

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
  assert(globals.seat != NULL);

  uint32_t capabilities = 0;

  assert(xdg_wm_base_add_listener(globals.xdg_wm_base, &xdg_wm_base_listener, NULL) == 0);
  assert(wl_seat_add_listener(globals.seat, &wl_seat_listener, &capabilities) == 0);

  assert(wl_display_roundtrip(wl_display) > 0);
  assert(capabilities != 0);

  struct wl_display *display = wl_display_create();
  assert(display != NULL);

  wl_global_create(display, &wl_compositor_interface, 6, globals.compositor, wl_compositor_on_bind);
  wl_global_create(display, &wl_shm_interface, 2, globals.shm, wl_shm_on_bind);
  wl_global_create(display, &xdg_wm_base_interface, 7, globals.xdg_wm_base, xdg_wm_base_on_bind);
  wl_global_create(display, &wl_seat_interface, 10, globals.seat, wl_seat_on_bind);

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
