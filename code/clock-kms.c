#include <assert.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "draw-clock.h"

static
void
page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data) {
  int *flip = data;
  *flip = 1;
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
  assert(crtc->width >= 400);
  assert(crtc->height >= 400);

  uint32_t width = crtc->width;
  uint32_t height = crtc->height;
  uint32_t depth = 24;
  uint32_t bpp = 32;

  uint32_t handle;
  uint32_t pitch;
  uint64_t size;
  assert(drmModeCreateDumbBuffer(fd, width, height, bpp, 0, &handle, &pitch, &size) == 0);

  uint32_t fb_id;
  assert(drmModeAddFB(fd, width, height, depth, bpp, pitch, handle, &fb_id) == 0);

  uint64_t offset;
  assert(drmModeMapDumbBuffer(fd, handle, &offset) == 0);

  char *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, offset);
  assert(buf != MAP_FAILED);

  buf += (height - 400)/2 * pitch + 4 * ((width - 400)/2);

  cairo_surface_t *cr_surface = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32, 400, 400, pitch);
  assert(cr_surface != NULL);

  drmEventContext ev_ctx = {
    .version = DRM_EVENT_CONTEXT_VERSION,
    .page_flip_handler = page_flip_handler,
  };

  for (;;) {
    draw_clock(cr_surface);
    int flip = 0;
    assert(drmModePageFlip(fd, crtc_id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, &flip) == 0);
    while (!flip)
      assert(drmHandleEvent(fd, &ev_ctx) == 0);
  }

  return 0;
}
