#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <linux/pci.h>
#include "virtio.h"

static
void *
virtio_get_cfg(struct vfio_pci_dev *pci, struct virtio_pci_cap *cap) {
  char *buf = pci->bar[cap->bar];
  return buf + cap->offset;
}

static
uint64_t
read_feature(uint32_t volatile *select, uint32_t volatile *feature) {
  *select = 0;
  uint64_t lo = *feature;
  *select = 1;
  uint64_t hi = *feature;
  return (hi << 32) | lo;
}

static
void
write_feature(uint32_t volatile *select, uint32_t volatile *feature, uint64_t value) {
  *select = 0;
  *feature = value & 0xFFFFFFFF;
  *select = 1;
  *feature = value >> 32;
}

static
void
set_queue_addr(uint32_t volatile *lo, uint32_t volatile *hi, uint64_t addr) {
  *lo = addr & 0xFFFFFFFF;
  *hi = addr >> 32;
}

size_t
align_down(size_t addr, size_t align) {
  return addr - addr % align;
}

size_t
align_up(size_t addr, size_t align) {
  return align_down(addr + align - 1, align);
}


void
virtio_pci_dev_init(struct virtio_pci_dev *dev, struct vfio_pci_dev *pci, uint64_t feature) {
  dev->pci = pci;
  for (size_t i=0; i<10; ++i)
    dev->caps[i] = NULL;

  uint64_t driver_feature = feature | (1ULL<<VIRTIO_F_VERSION_1) | (1ULL<<VIRTIO_F_ACCESS_PLATFORM);

  uint8_t cap_next;
  vfio_pci_dev_read_config(pci, &cap_next, 1, PCI_CAPABILITY_LIST);
  while (cap_next > 0) {
    struct virtio_pci_cap cap = {0};
    vfio_pci_dev_read_config(pci, &cap, 4, cap_next);
    if (cap.cap_vndr == PCI_CAP_ID_VNDR) {
      void *buf = malloc(cap.cap_len);
      assert(buf != NULL);
      vfio_pci_dev_read_config(pci, buf, cap.cap_len, cap_next);
      dev->caps[cap.cfg_type] = buf;
    }
    cap_next = cap.cap_next;
  }

  assert(dev->caps[VIRTIO_PCI_CAP_COMMON_CFG] != NULL);
  struct virtio_pci_common_cfg volatile *common_cfg = virtio_get_cfg(pci, dev->caps[VIRTIO_PCI_CAP_COMMON_CFG]);
  assert(common_cfg != NULL);
  dev->common_cfg = common_cfg;

  assert(dev->caps[VIRTIO_PCI_CAP_NOTIFY_CFG] != NULL);
  struct virtio_pci_notify_cap *notify_cfg_cap = (struct virtio_pci_notify_cap *)dev->caps[VIRTIO_PCI_CAP_NOTIFY_CFG];
  uint32_t notify_off_multiplier = notify_cfg_cap->notify_off_multiplier;
  uint8_t volatile *notify_cfg = virtio_get_cfg(pci, dev->caps[VIRTIO_PCI_CAP_NOTIFY_CFG]);
  assert(notify_cfg != NULL);

  assert(dev->caps[VIRTIO_PCI_CAP_DEVICE_CFG] != NULL);
  void volatile *device_cfg = virtio_get_cfg(pci, dev->caps[VIRTIO_PCI_CAP_DEVICE_CFG]);
  assert(device_cfg != NULL);
  dev->device_cfg = device_cfg;

  // Driver Requirements: Device Initialization

  // 1. Reset the device
  common_cfg->device_status = 0;
  // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
  common_cfg->device_status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  // 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
  common_cfg->device_status |= VIRTIO_CONFIG_S_DRIVER;
  // 4. Read device feature bits
  uint64_t device_feature = read_feature(&common_cfg->device_feature_select, &common_cfg->device_feature);
  assert((device_feature & driver_feature) == driver_feature);
  // write the subset of feature bits understood by the OS and driver to the device
  write_feature(&common_cfg->guest_feature_select, &common_cfg->guest_feature, driver_feature);
  // 5. Set the FEATURES_OK status bit.
  common_cfg->device_status |= VIRTIO_CONFIG_S_FEATURES_OK;
  // 6. Re-read device status to ensure the FEATURES_OK bit is still set
  assert((common_cfg->device_status & VIRTIO_CONFIG_S_FEATURES_OK) == VIRTIO_CONFIG_S_FEATURES_OK);
  driver_feature = read_feature(&common_cfg->guest_feature_select, &common_cfg->guest_feature);
  // 7. Perform device-specific setup, including discovery of
  // virtqueues for the device, optional per-bus setup, reading and
  // possibly writing the device’s virtio configuration space, and
  // population of virtqueues.

  uint16_t num_queues = common_cfg->num_queues;
  common_cfg->msix_config = num_queues;
  assert(common_cfg->msix_config != VIRTIO_MSI_NO_VECTOR);

  struct virtio_queue *queues = malloc(sizeof(struct virtio_queue) * num_queues);
  dev->queues = queues;

  for (uint16_t i=0; i<num_queues; ++i) {
    // Virtqueue Configuration
    queues[i].index = i;

    // 1. Write the virtqueue index (first queue is 0) to queue_select.
    common_cfg->queue_select = i;
    // 2. Read the virtqueue size from queue_size
    uint16_t queue_size = common_cfg->queue_size;
    // If this field is 0, the virtqueue does not exist.
    assert(queue_size != 0);
    // 3. Optionally, select a smaller virtqueue size and write it to queue_size.
    common_cfg->queue_size = queue_size;

    queues[i].notify_addr = (uint32_t volatile *)&notify_cfg[notify_off_multiplier * common_cfg->queue_notify_off];

    // 4. Allocate and zero Descriptor Table, Available and Used rings for the virtqueue in contiguous physical memory
    unsigned size = vring_size(queue_size, 4096);
    void *addr = vfio_pci_dev_map_dma(pci, align_up(size, 4096), NULL);
    vring_init(&queues[i].vring, queue_size, addr, 4096);

    for (uint16_t j=0; j<queue_size; ++j) {
      queues[i].vring.desc[j].next = j+1;
      queues[i].vring.desc[j].flags |= VRING_DESC_F_NEXT;
    }

    queues[i].vring.desc[queue_size-1].flags ^= VRING_DESC_F_NEXT;

    queues[i].free_head = 0;
    queues[i].free_count = queue_size;
    queues[i].next_used = 0;
    queues[i].used_index = 0;
    queues[i].next_avail = 0;

    set_queue_addr(&common_cfg->queue_desc_lo, &common_cfg->queue_desc_hi, (uint64_t)queues[i].vring.desc);
    set_queue_addr(&common_cfg->queue_avail_lo, &common_cfg->queue_avail_hi, (uint64_t)queues[i].vring.avail);
    set_queue_addr(&common_cfg->queue_used_lo, &common_cfg->queue_used_hi, (uint64_t)queues[i].vring.used);

    // 5. Optionally, if MSI-X capability is present and enabled on the device, select a vector to use to request interrupts triggered by virtqueue events.
    assert(pci->irq_index == VFIO_PCI_MSIX_IRQ_INDEX);
    // Write the MSI-X Table entry number corresponding to this vector into queue_msix_vector.
    common_cfg->queue_msix_vector = i;
    // Read queue_msix_vector: on success, previously written value is returned; on failure, NO_VECTOR value is returned.
    assert(common_cfg->queue_msix_vector != VIRTIO_MSI_NO_VECTOR);

    common_cfg->queue_enable = 1;
  }
}

void
virtio_send_driver_ok(struct virtio_pci_dev *dev) {
  dev->common_cfg->device_status |= VIRTIO_CONFIG_S_DRIVER_OK;
}

int
virtio_queue_writev(struct virtio_queue *queue, const struct iovec *iov, int iovcnt, int readcnt) {
  assert(readcnt <= iovcnt);
  if (queue->free_count < iovcnt)
    return -1;

  uint16_t first = queue->free_head;
  uint16_t index = first;

  vring_desc_t *desc = queue->vring.desc;

  for (int i=0; i<iovcnt; ++i) {
    --(queue->free_count);

    desc[index].addr = (__virtio64)iov[i].iov_base;
    desc[index].len = iov[i].iov_len;

    if (i < readcnt) {
      desc[index].flags ^= (desc[index].flags & VRING_DESC_F_WRITE);
    } else {
      desc[index].flags |= VRING_DESC_F_WRITE;
    }

    assert(desc[index].flags & VRING_DESC_F_NEXT);
    if (i + 1 == iovcnt)
      desc[index].flags ^= VRING_DESC_F_NEXT;

    index = desc[index].next;
  }

  queue->free_head = index;
  return first;
}

void
virtio_queue_send(struct virtio_queue *queue, uint16_t index) {
  // 2. The driver places the index of the head of the descriptor chain into the next ring entry of the available ring.
  queue->vring.avail->ring[queue->next_avail % queue->vring.num] = index;

  // 4. The driver performs a suitable memory barrier to ensure the device sees the updated descriptor table and available ring before the next step.
  // 5. The available idx is increased by the number of descriptor chain heads added to the available ring.
  ++(queue->next_avail);
  atomic_store_explicit(&queue->vring.avail->idx, queue->next_avail, memory_order_release);

  // 6. The driver performs a suitable memory barrier to ensure that it updates the idx field before checking for notification suppression.
  atomic_thread_fence(memory_order_seq_cst);

  uint16_t flags = atomic_load_explicit(&queue->vring.used->flags, memory_order_acquire);

  // 7. The driver sends an available buffer notification to the device if such notifications are not suppressed.
  if (!(flags & VRING_USED_F_NO_NOTIFY))
    *queue->notify_addr = queue->index;
}

struct vring_used_elem *
virtio_queue_recv(struct virtio_queue *queue) {
  if (queue->next_used == queue->used_index)
    return NULL;

  struct vring_used_elem *used = &queue->vring.used->ring[queue->next_used % queue->vring.num];
  ++(queue->next_used);
  return used;
}

void
virtio_queue_free(struct virtio_queue *queue, uint16_t first) {
  vring_desc_t *desc = queue->vring.desc;
  uint16_t index = first;
  for ( ; desc[index].flags&VRING_DESC_F_NEXT; index=desc[index].next)
    ++(queue->free_count);
  ++(queue->free_count);
  desc[index].next = queue->free_head;
  desc[index].flags |= VRING_DESC_F_NEXT;
  queue->free_head = first;
}

void
virtio_queue_handle_event(struct virtio_queue *queue) {
  queue->used_index = atomic_load_explicit(&queue->vring.used->idx, memory_order_acquire);
}
