#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#define EGL_EGL_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "generated/xdg-shell-protocol.h"
#include "gl-draw-clock.h"

struct globals {
  struct wl_compositor *compositor;
  struct xdg_wm_base *xdg_wm_base;
};

static
void
registry_global(void *data, struct wl_registry *reg, uint32_t id, const char *interface, uint32_t version) {
  struct globals *globals = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    globals->compositor = wl_registry_bind(reg, id, &wl_compositor_interface, version);
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
  EGLDisplay egl_display;
  EGLSurface egl_surface;
};

static
struct wl_callback_listener frame_callback_listener;

static
void
request_frame(struct frame_context *context) {
  struct wl_callback *cb = wl_surface_frame(context->wl_surface);
  wl_callback_add_listener(cb, &frame_callback_listener, context);
  gl_draw_clock();
  eglSwapBuffers(context->egl_display, context->egl_surface);
}

static
void
frame_callback_done(void *data, struct wl_callback *cb, uint32_t time) {
  wl_callback_destroy(cb);
  struct frame_context *context = data;
  request_frame(context);
}

static
struct wl_callback_listener frame_callback_listener = {
  .done = frame_callback_done
};

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
  assert(globals.xdg_wm_base != NULL);

  struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
  };

  assert(xdg_wm_base_add_listener(globals.xdg_wm_base, &xdg_wm_base_listener, NULL) == 0);
  assert(wl_display_roundtrip(wl_display) > 0);

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

  struct wl_egl_window *egl_window = wl_egl_window_create(wl_surface, width, height);
  assert(egl_window != NULL);

  EGLDisplay egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wl_display, NULL);
  assert(egl_display != EGL_NO_DISPLAY);

  EGLint major;
  EGLint minor;
  assert(eglInitialize(egl_display, &major, &minor) == EGL_TRUE);
  fprintf(stderr, "EGL Version %d.%d\n", major, minor);

  assert(eglBindAPI(EGL_OPENGL_API) == EGL_TRUE);

  EGLint config_attributes[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_ALPHA_SIZE,      8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE,
  };

  EGLint count;
  EGLConfig egl_config;

  assert(eglChooseConfig(egl_display, &config_attributes[0], &egl_config, 1, &count) == EGL_TRUE);
  assert(count > 0);

  EGLSurface egl_surface = eglCreateWindowSurface(egl_display, egl_config, egl_window, NULL);
  assert(egl_surface != EGL_NO_SURFACE);

  EGLint context_attributes[] = {
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_CONTEXT_CLIENT_VERSION,      4,
    EGL_CONTEXT_MINOR_VERSION_KHR,   3,
    EGL_NONE,
  };

  EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, &context_attributes[0]);
  assert(egl_context != EGL_NO_CONTEXT);
  assert(eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) == EGL_TRUE);

  gl_draw_clock_init();

  struct frame_context context = {
    .wl_surface = wl_surface,
    .egl_display = egl_display,
    .egl_surface = egl_surface,
  };

  request_frame(&context);

  for (;;)
    wl_display_dispatch(wl_display);

  return 0;
}
