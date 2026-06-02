#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#include "vfio.h"
#include "edu.h"

static
void
generate_random(uint32_t *random) {
  int fd = open("/dev/urandom", O_RDONLY);
  assert(fd >= 0);
  assert(read(fd, random, sizeof(uint32_t)) == sizeof(uint32_t));
  close(fd);
}

static
int
wait_event(struct vfio_pci_dev *dev, int timeout) {
  int64_t n = vfio_pci_dev_read_event(dev, timeout);
  if (n < 0)
    return -1;

  assert(n == 0);
  vfio_pci_dev_clear_irq(dev, 0);
  return 0;
}

static
void
_log(char const *format, ...) {
  struct timespec  ts;
  clock_gettime(CLOCK_BOOTTIME, &ts);
  printf("[%5lld.%06lld] ", ts.tv_sec, ts.tv_nsec/1000);

  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  va_end(ap);
}

static
void
print_bits(int n, uint32_t x) {
  for (int i=n-1; i>=0; --i)
    printf("%d", ((x>>i)&1));
}

int
main(int argc, char **argv) {
  printf("********** OPEN EDU DEVICE **********\n");
  struct vfio_pci_dev dev = {0};
  vfio_pci_dev_open(((argc==1)?"0000:00:04.0":argv[1]), &dev);
  vfio_pci_dev_init(&dev);

  printf("********** CHECK DEVICE VERSION **********\n");
  struct edu_cfg volatile *cfg = dev.bar[0];
  uint32_t id = cfg->identification;
  assert((id & 0xFFFF) == 0xEDU);
  printf("Identification: 0xedU\n");

  uint16_t version = id >> 16;
  printf("Version:        %u.%u\n", (version >> 8), (version & 0xFF));

  printf("************* CHECK LIVENESS *************\n");
  uint32_t random = 0;
  generate_random(&random);

  cfg->liveness = random;
  printf("Set ");
  print_bits(32, random);
  printf("\n");

  uint32_t liveness = cfg->liveness;
  printf("Get ");
  print_bits(32, liveness);
  printf("\n");

  assert(liveness == ~random);

  liveness ^= random;
  printf("Xor ");
  print_bits(32, liveness);
  printf("\n");

  printf("************ CHECK  FACTORIAL ************\n");
  cfg->factorial_status = 0;
  cfg->interrupt_acknowledge = cfg->interrupt_status;
  _log("| factorial status | interrupt status |\n");
  _log("| fedcba9876543210 | fedcba9876543210 |\n");
  _log("| ");
  print_bits(16, cfg->factorial_status);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");
  _log("Enable interrupt\n");
  cfg->factorial_status |= 0x80;
  _log("| ");
  print_bits(16, cfg->factorial_status);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");
  uint32_t factorial = (random & 0xF) + 1;
  _log("Set factorial=%u\n", factorial);
  cfg->factorial = factorial;
  _log("| ");
  print_bits(16, cfg->factorial_status);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");
  _log("Get factorial=%u\n", cfg->factorial);

  _log("Wait interrupt\n");
  assert(wait_event(&dev, -1) == 0);

  _log("Interrupted!!!\n");
  _log("| ");
  print_bits(16, cfg->factorial_status);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");
  _log("Get factorial=%u\n", cfg->factorial);

  cfg->interrupt_acknowledge = cfg->interrupt_status;
  _log("Clear interrupt status\n");
  _log("| ");
  print_bits(16, cfg->factorial_status);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");

  _log("Wait interrupt\n");
  assert(wait_event(&dev, 1000) < 0);
  _log("Time out\n");

  printf("********** CHECK DMA ROUNDTRIP **********\n");
  cfg->dma_command = 0;
  cfg->interrupt_acknowledge = cfg->interrupt_status;

  __u64 iova = 0;
  __u64 dma_iova = iova;
  void *dma_addr = vfio_pci_dev_map_dma(&dev, &iova, 4096, -1, 0);

  uint32_t *src_buf = (uint32_t *)dma_addr;
  uint32_t *dst_buf = src_buf + 16;
  for (int i=0; i<16; i++)
    src_buf[i] = i + random;

  _log("src=");
  for(int i=0; i<8; i++)
    printf("%08x ", src_buf[i]);
  printf("\n");
  _log("    ");
  for(int i=8; i<16; i++)
    printf("%08x ", src_buf[i]);
  printf("\n");

  _log("dst=");
  for(int i=0; i<8; i++)
    printf("%08x ", dst_buf[i]);
  printf("\n");
  _log("    ");
  for(int i=8; i<16; i++)
    printf("%08x ", dst_buf[i]);
  printf("\n");

  _log("|   dma  command   | interrupt status |\n");
  _log("| fedcba9876543210 | fedcba9876543210 |\n");

  _log("Start DMA Write\n");
  cfg->dma_src = dma_iova;
  cfg->dma_dst = 0x40000;
  cfg->dma_len = sizeof(uint32_t) * 16;
  cfg->dma_command = 0x1 | 0x0 | 0x4;

  _log("| ");
  print_bits(16, cfg->dma_command);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");

  _log("Wait Interrupt\n");
  if (wait_event(&dev, -1) < 0)
    return 1;

  _log("Interrupted!!!\n");
  _log("| ");
  print_bits(16, cfg->dma_command);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");


  _log("Clear interrupt status\n");
  cfg->interrupt_acknowledge = cfg->interrupt_status;

  _log("| ");
  print_bits(16, cfg->dma_command);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");


  _log("Start DMA Read\n");
  cfg->dma_src = 0x40000;
  cfg->dma_dst = dma_iova + sizeof(uint32_t) * 16;
  cfg->dma_len = sizeof(uint32_t) * 16;
  cfg->dma_command = 0x1 | 0x2 | 0x4;


  _log("| ");
  print_bits(16, cfg->dma_command);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");

  _log("Wait Interrupt\n");

  assert(wait_event(&dev, -1) == 0);

  _log("Interrupted!!!\n");
  _log("| ");
  print_bits(16, cfg->dma_command);
  printf(" | ");
  print_bits(16, cfg->interrupt_status);
  printf(" |\n");


  _log("src=");
  for(int i=0; i<8; i++)
    printf("%08x ", src_buf[i]);
  printf("\n");
  _log("    ");
  for(int i=8; i<16; i++)
    printf("%08x ", src_buf[i]);
  printf("\n");

  _log("dst=");
  for(int i=0; i<8; i++)
    printf("%08x ", dst_buf[i]);
  printf("\n");
  _log("    ");
  for(int i=8; i<16; i++)
    printf("%08x ", dst_buf[i]);
  printf("\n");

  vfio_pci_dev_unmap_dma(&dev, dma_iova, 4096);
  assert(munmap(dma_addr, 4096) == 0);
  vfio_pci_dev_close(&dev);
  return 0;
}
