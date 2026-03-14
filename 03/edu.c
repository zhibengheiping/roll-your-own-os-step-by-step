#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#include "vfio.h"
#include "edu.h"

int
generate_random(uint32_t *random) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return fd;

  int result = read(fd, random, sizeof(uint32_t));
  close(fd);
  return result;
}

int
wait_event(int fd, int timeout) {
  struct epoll_event events[1];
  int nfd = epoll_wait(fd, events, 1, timeout);
  if (!nfd)
    return -1;

  uint64_t counter = 0;
  read(events[0].data.fd, &counter, sizeof(uint64_t));
  return 0;
}


int
setup_epoll(struct vfio_pci_dev *dev) {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  for(__u32 i=0; i<dev->num_irqs; i++) {
    struct epoll_event event = {
      .events = EPOLLIN,
      .data = {.fd = dev->eventfd[i]},
    };
    epoll_ctl(fd, EPOLL_CTL_ADD, dev->eventfd[i], &event);
  }
  return fd;
}

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

void
print_bits(int n, uint32_t x) {
  for (int i=n-1; i>=0; --i)
    printf("%d", ((x>>i)&1));
}


int
main(int argc, char **argv) {
  printf("********** OPEN EDU DEVICE **********\n");
  struct vfio_pci_dev dev = {0};
  int result = vfio_pci_dev_open(((argc==1)?"0000:00:04.0":argv[1]), &dev);
  if (result < 0)
    return 1;
  vfio_print_info(&dev);
  result = vfio_pci_dev_init(&dev);
  if (result < 0)
    return 1;

  printf("********** CHECK DEVICE VERSION **********\n");
  struct edu_cfg volatile *cfg = dev.bar[0];
  uint32_t id = cfg->identification;
  if ((id & 0xFFFF) != 0xEDU) {
    fprintf(stderr, "ID is not EDU\n");
    return 1;
  }
  printf("Identification: 0xedU\n");

  uint16_t version = id >> 16;
  printf("Version:        %u.%u\n", (version >> 8), (version & 0xFF));

  printf("************* CHECK LIVENESS *************\n");
  uint32_t random = 0;
  result = generate_random(&random);
  if (result < 0)
    return 1;

  cfg->liveness = random;
  printf("Set ");
  print_bits(32, random);
  printf("\n");

  uint32_t liveness = cfg->liveness;
  printf("Get ");
  print_bits(32, liveness);
  printf("\n");

  if (liveness != ~random)
    return 1;

  liveness ^= random;
  printf("Xor ");
  print_bits(32, liveness);
  printf("\n");

  printf("************ CHECK  FACTORIAL ************\n");
  int fd = setup_epoll(&dev);
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
  if (wait_event(fd, -1) != 0)
    return 1;

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
  if (wait_event(fd, 1000) == 0)
    return 1;
  _log("Time out\n");

  printf("********** CHECK DMA ROUNDTRIP **********\n");
  cfg->dma_command = 0;
  cfg->interrupt_acknowledge = cfg->interrupt_status;

  __u64 iova = 0;
  __u64 dma_iova = iova;
  void *dma_addr = vfio_pci_dev_map_dma(&dev, 4096, &iova);

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
  if (wait_event(fd, -1) < 0)
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

  if (wait_event(fd, -1) < 0)
    return 1;

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
  return 0;
}
