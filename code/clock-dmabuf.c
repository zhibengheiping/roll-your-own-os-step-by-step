#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client-core.h>
#include <wayland-egl-core.h>
#include <gbm.h>
#include <libudev.h>

#define EGL_EGL_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include "generated/xdg-shell-client-protocol.h"
#include "generated/linux-dmabuf-v1-client-protocol.h"
#include "gl-draw-clock.h"

struct globals {
  struct wl_compositor *compositor;
  struct xdg_wm_base *xdg_wm_base;
  struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf_v1;
};

static
void
registry_global(void *data, struct wl_registry *reg, uint32_t id, const char *interface, uint32_t version) {
  struct globals *globals = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    globals->compositor = wl_registry_bind(reg, id, &wl_compositor_interface, version);
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    globals->xdg_wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, version);
  } else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
    globals->zwp_linux_dmabuf_v1= wl_registry_bind(reg, id, &zwp_linux_dmabuf_v1_interface, version);
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
};

static
struct wl_callback_listener frame_callback_listener;

static
void
request_frame(struct frame_context *context) {
  struct wl_callback *cb = wl_surface_frame(context->wl_surface);
  wl_callback_add_listener(cb, &frame_callback_listener, context);
  gl_draw_clock();
  wl_surface_attach(context->wl_surface, context->wl_buffer, 0, 0);
  wl_surface_damage_buffer(context->wl_surface, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(context->wl_surface);
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

struct format_table_entry {
  uint32_t format;
  uint32_t padding;
  uint64_t modifier;
};

struct feedback_tranche {
  dev_t target_device;
  uint32_t flags;
  size_t n_formats;
  uint16_t *formats;
};

struct feedback_context {
  int done;
  struct format_table_entry *format_table;
  dev_t main_device;
  size_t n_tranches;
  struct feedback_tranche *tranches;
};

static
void
feedback_context_init(struct feedback_context *context) {
  context->done = 0;
  context->format_table = NULL;
  context->main_device = -1;
  context->n_tranches = 0;
  context->tranches = malloc(sizeof(struct feedback_tranche));
  context->tranches[context->n_tranches].target_device = -1;
  context->tranches[context->n_tranches].flags = 0;
  context->tranches[context->n_tranches].n_formats = 0;
  context->tranches[context->n_tranches].formats = NULL;
}

static
void
feedback_format_table(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback, int fd, uint32_t size) {
  struct feedback_context *context = data;
  void *buf = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(buf != MAP_FAILED);
  context->format_table = buf;
}

static
void
feedback_main_device(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device) {
  struct feedback_context *context = data;
  assert(device->size >= sizeof(dev_t));
  memcpy(&context->main_device, device->data, sizeof(dev_t));
}

static
void
feedback_tranche_target_device(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device) {
  struct feedback_context *context = data;
  assert(device->size >= sizeof(dev_t));
  memcpy(&context->tranches[context->n_tranches].target_device, device->data, sizeof(dev_t));
}

static
void
feedback_tranche_flags(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback, uint32_t flags) {
  struct feedback_context *context = data;
  context->tranches[context->n_tranches].flags = flags;
}

static
void
feedback_tranche_formats(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *indices) {
  struct feedback_context *context = data;
  size_t n = context->tranches[context->n_tranches].n_formats;
  size_t grow = indices->size / sizeof(uint16_t);
  context->tranches[context->n_tranches].formats = realloc(context->tranches[context->n_tranches].formats, sizeof(uint16_t) * (n+grow));
  memcpy(&context->tranches[context->n_tranches].formats[n], indices->data, indices->size/sizeof(uint16_t) * sizeof(uint16_t));
  context->tranches[context->n_tranches].n_formats += grow;
}

static
void
feedback_tranche_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback) {
  struct feedback_context *context = data;
  context->n_tranches += 1;
  context->tranches = realloc(context->tranches, sizeof(struct feedback_tranche) * (context->n_tranches + 1));
  context->tranches[context->n_tranches].target_device = -1;
  context->tranches[context->n_tranches].flags = 0;
  context->tranches[context->n_tranches].n_formats = 0;
  context->tranches[context->n_tranches].formats = NULL;
}

static
void
feedback_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback) {
  struct feedback_context *context = data;
  context->done = 1;
}

struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
  .format_table = feedback_format_table,
  .main_device = feedback_main_device,
  .tranche_target_device = feedback_tranche_target_device,
  .tranche_flags = feedback_tranche_flags,
  .tranche_formats = feedback_tranche_formats,
  .tranche_done = feedback_tranche_done,
  .done = feedback_done,
};

static
dev_t
find_dmabuf_device(struct feedback_context *context) {
  for (size_t i=0; i<context->n_tranches; ++i) {
    struct feedback_tranche *p = &context->tranches[i];
    for (size_t j=0; i<p->n_formats; ++j)
      if (context->format_table[p->formats[j]].format == GBM_FORMAT_ARGB8888)
        return p->target_device;
  }
  return -1;
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
  assert(globals.xdg_wm_base != NULL);
  assert(globals.zwp_linux_dmabuf_v1 != NULL);

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

  struct zwp_linux_dmabuf_feedback_v1 *feedback = zwp_linux_dmabuf_v1_get_surface_feedback(globals.zwp_linux_dmabuf_v1, wl_surface);

  struct feedback_context feedback_context;
  feedback_context_init(&feedback_context);

  assert(zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &feedback_listener, &feedback_context) == 0);
  assert(wl_display_roundtrip(wl_display) > 0);
  assert(feedback_context.done);

  dev_t dev = find_dmabuf_device(&feedback_context);
  assert(dev != -1);

  struct udev *udev = udev_new();
  struct udev_device *udev_device = udev_device_new_from_devnum(udev, 'c', dev);
  const char *node = udev_device_get_devnode(udev_device);

  int gbm_fd = open(node, O_RDWR);
  udev_device_unref(udev_device);
  udev_unref(udev);
  assert(gbm_fd>0);

  struct gbm_device *gbm = gbm_create_device(gbm_fd);
  assert(gbm != NULL);

  struct gbm_bo *bo = gbm_bo_create(gbm, 400, 400, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
  assert(bo != NULL);
  struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(globals.zwp_linux_dmabuf_v1);
  assert(params != NULL);
  int fd = gbm_bo_get_fd(bo);
  assert(fd >= 0);

  uint32_t stride = gbm_bo_get_stride(bo);
  uint64_t modifier = gbm_bo_get_modifier(bo);
  uint32_t modifier_lo = modifier & 0xFFFFFFFF;
  uint32_t modifier_hi = modifier >> 32;
  int plane_count = gbm_bo_get_plane_count(bo);
  for (int i=0; i<plane_count; ++i) {
    uint32_t offset = gbm_bo_get_offset(bo, i);
    zwp_linux_buffer_params_v1_add(params, fd, i, offset, stride, modifier_hi, modifier_lo);
  }

  uint32_t width = gbm_bo_get_width(bo);
  uint32_t height = gbm_bo_get_height(bo);
  uint32_t format = gbm_bo_get_format(bo);

  struct wl_buffer *wl_buffer = zwp_linux_buffer_params_v1_create_immed(params, width, height, format, 0);
  assert(wl_buffer != NULL);

  EGLDisplay egl_display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
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

  EGLAttrib attributes[] = {
    EGL_WIDTH,                          width,
    EGL_HEIGHT,                         height,
    EGL_LINUX_DRM_FOURCC_EXT,           format,
    EGL_DMA_BUF_PLANE0_FD_EXT,          fd,
    EGL_DMA_BUF_PLANE0_OFFSET_EXT,      0,
    EGL_DMA_BUF_PLANE0_PITCH_EXT,       stride,
    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, modifier_hi,
    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, modifier_lo,
    EGL_DMA_BUF_PLANE1_FD_EXT,          fd,
    EGL_DMA_BUF_PLANE1_OFFSET_EXT,      0,
    EGL_DMA_BUF_PLANE1_PITCH_EXT,       stride,
    EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, modifier_hi,
    EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, modifier_lo,
    EGL_DMA_BUF_PLANE2_FD_EXT,          fd,
    EGL_DMA_BUF_PLANE2_OFFSET_EXT,      0,
    EGL_DMA_BUF_PLANE2_PITCH_EXT,       stride,
    EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, modifier_hi,
    EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, modifier_lo,
    EGL_DMA_BUF_PLANE3_FD_EXT,          fd,
    EGL_DMA_BUF_PLANE3_OFFSET_EXT,      0,
    EGL_DMA_BUF_PLANE3_PITCH_EXT,       stride,
    EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT, modifier_hi,
    EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT, modifier_lo,
    EGL_NONE,
  };

  for (int i=0; i<plane_count; ++i) {
    uint32_t offset = gbm_bo_get_offset(bo, i);
    attributes[9 + 10 * i] = offset;
  }
  attributes[6 + 10 * plane_count] = EGL_NONE;
  EGLImage image = eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, &attributes[0]);
  assert(image != EGL_NO_IMAGE);

  EGLint context_attributes[] = {
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_CONTEXT_CLIENT_VERSION,      4,
    EGL_CONTEXT_MINOR_VERSION_KHR,   3,
    EGL_NONE,
  };

  EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, &context_attributes[0]);
  assert(egl_context != EGL_NO_CONTEXT);
  assert(eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context) == EGL_TRUE);

  gl_draw_clock_init();

  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);

  GLuint fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_FLIP_Y_MESA, GL_TRUE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glViewport(0, 0, width, height);

  struct frame_context context = {
    .wl_surface = wl_surface,
    .wl_buffer = wl_buffer,
  };

  request_frame(&context);

  for (;;)
    wl_display_dispatch(wl_display);

  return 0;
}
