#include <assert.h>
#include <stddef.h>

#include "virtio-input.h"

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
supply_buffer(struct virtio_queue *queue, void *buf, size_t len, int read) {
  struct iovec iovec = {
    .iov_base = buf,
    .iov_len = len,
  };

  int index = virtio_queue_alloc(queue, 1);
  assert(index >= 0);
  virtio_queue_writev(queue, index, &iovec, 1, read);
  virtio_queue_send(queue, index);
}

int
virtio_input_recv(struct virtio_input_dev *dev, struct virtio_input_event *event) {
  struct vring_used_elem *elem = virtio_queue_recv(&dev->virtio.queues[0]);
  if (elem == NULL)
    return 0;

  *event = dev->events[elem->id];
  virtio_queue_free(&dev->virtio.queues[0], elem->id);
  supply_buffer(&dev->virtio.queues[0], &dev->events[elem->id], sizeof(struct virtio_input_event), 0);

  return 1;
}

void
virtio_input_dev_init(struct virtio_input_dev *dev, struct vfio_pci_dev *pci) {
  virtio_pci_dev_init(&dev->virtio, pci, 0);

  struct virtio_queue *queue = &dev->virtio.queues[0];
  size_t size = sizeof(struct virtio_input_event) * queue->vring.num;

  struct virtio_input_event *events = vfio_pci_dev_map_dma(pci, NULL, align_up(size, 4096), -1, 0);

  dev->events = events;

  virtio_send_driver_ok(&dev->virtio);

  for (unsigned int i=0; i<queue->vring.num; ++i)
    supply_buffer(queue, &events[i], sizeof(struct virtio_input_event), 0);
}
