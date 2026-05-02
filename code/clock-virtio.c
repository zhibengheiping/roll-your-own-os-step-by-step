#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include <stdio.h>
#include <unistd.h>

#include "virtio-gpu.h"
#include "draw-clock.h"

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

int
main(void) {
  struct vfio_pci_dev pci = {0};
  char const *devid = getenv("DEVID");
  if (devid == NULL)
    devid = "0000:00:03.0";

  vfio_pci_dev_open(devid, &pci);
  vfio_pci_dev_init(&pci);

  struct virtio_gpu_dev dev = {0};
  virtio_gpu_dev_init(&dev, &pci);

  size_t size = 400 * 400 * 4;
  char *buf = vfio_pci_dev_map_dma(&pci, align_up(size, 4096), NULL);

  virtio_gpu_resource_create_2d(&dev, 1, 400, 400);
  virtio_gpu_resource_attach_backing(&dev, 1, buf, size);
  virtio_gpu_set_scanout(&dev, 1, 400, 400);

  cairo_surface_t *surface = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32, 400, 400, 400 * 4);
  for (;;) {
    draw_clock(surface);
    virtio_gpu_transfer_to_host_2d(&dev, 1, 400, 400);
    virtio_gpu_resource_flush(&dev, 1, 400, 400);
  }
}
