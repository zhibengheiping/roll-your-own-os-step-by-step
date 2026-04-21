#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <gbm.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#define EGL_EGL_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include "gl-draw-clock.h"

static
void
page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data) {
  int *flip = data;
  *flip = 1;
}

static
void
destroy_bo(struct gbm_bo *bo, void *data) {
  uint32_t fb_id = (uint32_t)(uintptr_t)data;
  struct gbm_device *gbm = gbm_bo_get_device(bo);
  assert(gbm != NULL);
  int fd = gbm_device_get_fd(gbm);
  assert(fd >= 0);
  assert(drmModeRmFB(fd, fb_id) == 0);
}

int
main(void) {
  int fd = open("/dev/dri/card0", O_RDWR|O_CLOEXEC);
  assert(fd >= 0);

  drmModeResPtr resources = drmModeGetResources(fd);
  assert(resources != NULL);

  uint32_t crtc_id = resources->crtcs[0];

  drmModeCrtcPtr crtc = drmModeGetCrtc(fd, crtc_id);
  assert(crtc != NULL);

  uint32_t width = crtc->width;
  uint32_t height = crtc->height;
  uint32_t depth = 24;
  uint32_t bpp = 32;
  assert(width >= 400);
  assert(height >= 400);

  struct gbm_device *gbm = gbm_create_device(fd);
  assert(gbm != NULL);

  struct gbm_surface *gbm_surface = gbm_surface_create(gbm, width, height, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  assert(gbm_surface != NULL);

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

  EGLSurface egl_surface = eglCreateWindowSurface(egl_display, egl_config, gbm_surface, NULL);
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

  glEnable(GL_SCISSOR_TEST);
  glScissor((width-400)/2, (height-400)/2, 400, 400);
  glViewport((width-400)/2, (height-400)/2, 400, 400);

  drmEventContext ev_ctx = {
    .version = DRM_EVENT_CONTEXT_VERSION,
    .page_flip_handler = page_flip_handler,
  };

  for (;;) {
    gl_draw_clock();
    eglSwapBuffers(egl_display, egl_surface);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surface);
    assert(bo != NULL);

    uint32_t fb_id = (uint32_t)(uintptr_t)gbm_bo_get_user_data(bo);
    if (fb_id == 0) {
      assert(drmModeAddFB(fd, width, height, depth, bpp, gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, &fb_id) == 0);
      gbm_bo_set_user_data(bo, (void*)(uintptr_t)fb_id, destroy_bo);
    }

    int flip = 0;
    assert(drmModePageFlip(fd, crtc_id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, &flip) == 0);
    while (!flip)
      assert(drmHandleEvent(fd, &ev_ctx) == 0);
    gbm_surface_release_buffer(gbm_surface, bo);
  }

  return 0;
}
