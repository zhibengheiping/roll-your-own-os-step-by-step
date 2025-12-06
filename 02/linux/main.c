#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include "../osrt.h"
#include "signal.h"

#define VIRTUAL_START 0x400000000000ULL
#define MEM_SIZE 0x1000000
#define PAGE_SIZE 0x1000
#define PG_ADDR(x) ((x)&0xFFFFFFFFFFFFF000ULL)
#define PG_P 0x001
#define PG_W 0x002
#define PG_U 0x004

uintptr_t pml4[PAGE_SIZE/sizeof(uintptr_t)] __attribute__((aligned(PAGE_SIZE))) = {0};

static int const REG_INDEX[8] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI};
#define REG64(x) (ucp->uc_mcontext.gregs[(REG_##x)])

static uintptr_t mmap_min_addr;
static uint64_t CR3;

static
int
x86_scr(ucontext_t *ucp, uint8_t modrm) {
  uint8_t rm = (modrm >> 3) & 0x7;
  if (rm != 3)
    return -1;

  REG64(INDEX[modrm & 0x7]) = CR3;
  return 0;
}

static
int
x86_lcr(ucontext_t *ucp, uint8_t modrm) {
  uint8_t rm = (modrm >> 3) & 0x7;
  if (rm != 3)
    return -1;

  greg_t value = REG64(INDEX[modrm & 0x7]);
  CR3 = value;
  if (mmap((void*)mmap_min_addr, VIRTUAL_START-mmap_min_addr, PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
    return -1;

  return 0;
}

static
int
x86_sysret(ucontext_t *ucp) {
  REG64(EFL) = REG64(R11);
  REG64(RIP) = REG64(RCX);
  return 1;
}

static
void *
lookup(uintptr_t *entry, uintptr_t addr) {
  char offsets[4] = {39, 30, 21, 12};

  for (size_t i=0; i<sizeof(offsets); ++i) {
    uint16_t index = (addr >> offsets[i]) & 0x1FF;
    if (!(entry[index] & PG_P))
      return NULL;
    entry = (uintptr_t *)(VIRTUAL_START + PG_ADDR(entry[index]));
  }

  return entry;
}

static
void
segfault_handler(int signo, siginfo_t *info, void *context) {
  ucontext_t *ucp = (ucontext_t *)context;
  switch (info->si_code) {
  case SI_KERNEL:
    if (REG64(TRAPNO) == 0x0D) {
      int result = -1;
      uint8_t *code = (uint8_t *)REG64(RIP);
      switch (*code++) {
      case 0x0F: switch (*code++) {
        case 0x20: result = x86_scr(ucp, *code++); break;
        case 0x22: result = x86_lcr(ucp, *code++); break;
        }
        break;
      case 0x48: switch(*code++) {
        case 0x0F: switch(*code++) {
          case 0x07: result = x86_sysret(ucp); break;
          }
        }
        break;
      }

      if (result == 0)
        REG64(RIP) = (greg_t)code;
      if (result >= 0)
        return;

    }
    break;
  case SEGV_ACCERR: {
      uintptr_t addr = (uintptr_t)info->si_addr;
      if (addr >= VIRTUAL_START)
        break;
      addr &= (UINTPTR_MAX ^ 0xFFF);
      uintptr_t *table = (uintptr_t *)(VIRTUAL_START + CR3);
      void *base = lookup(table, addr);
      if (base != NULL)
        if (mremap(base, PAGE_SIZE, PAGE_SIZE, MREMAP_FIXED|MREMAP_DONTUNMAP|MREMAP_MAYMOVE, (void*)addr) != MAP_FAILED)
          return;
      break;
  }
  }

  fprintf(stderr, "failed to handle segv %d\n", info->si_code);
  exit(EXIT_FAILURE);
}

static ssize_t (*origin_write)(int fd, void const *buf, size_t count) = NULL;

ssize_t
write(int fd, void const *buf, size_t count) {
  uintptr_t addr = (uintptr_t)buf;
  if ((fd == STDOUT_FILENO) && (addr < VIRTUAL_START))
    for(uintptr_t p = addr & (UINTPTR_MAX ^ 0xFFF); p<addr+count; p+=0x1000)
      atomic_load((char *)p);
  return origin_write(fd, buf, count);
}

int
main(int argc, char const **argv) {
  origin_write = dlsym(RTLD_NEXT, "write");

  FILE *f = fopen("/proc/sys/vm/mmap_min_addr", "r");
  if (!f)
    return 1;
  int result = fscanf(f, "%" SCNuPTR, &mmap_min_addr);
  fclose(f);
  if (result != 1)
    return 1;

  if (mmap((void*)mmap_min_addr, VIRTUAL_START-mmap_min_addr, PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
    return 1;

  if (mmap((void*)VIRTUAL_START, MEM_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED|MAP_SHARED|MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
    return 1;

  struct boot_mem boot_mem = {
    .base = (char *)VIRTUAL_START,
    .size = MEM_SIZE,
  };

  struct boot_info info = {
    .stack_top = VIRTUAL_START,
    .virtual_start = VIRTUAL_START,
    .initrd = NULL,
    .nmem = 1,
    .mem = &boot_mem,
  };

  if (argc > 1) {
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0)
      return 1;
    off_t off = lseek(fd, 0, SEEK_END);
    if (off < 0)
      return 1;

    void *addr = mmap(NULL, off, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED)
      return 1;

    info.initrd = addr;
  }

  static const char greet[] = "\x1b[2J\x1b[H";
  write(STDOUT_FILENO, greet, sizeof(greet));
  signal_init();

  CR3 = ((uintptr_t)pml4) - VIRTUAL_START;
  signal_set_handler(SIGSEGV, segfault_handler);
  kernel_main(&info);
  return 0;
}

static uintptr_t syscall_handler;

static
void
sys_handler(int signo, siginfo_t *info, void *context) {
  ucontext_t *ucp = (ucontext_t *)context;
  REG64(R11) = REG64(EFL);
  REG64(RCX) = REG64(RIP);
  REG64(RIP) = syscall_handler;
}

void
syscall_set_handler(void *handler) {
  syscall_handler = (uintptr_t)handler;
  signal_set_handler(SIGSYS, sys_handler);
  syscall(SYS_prctl, PR_SET_SYSCALL_USER_DISPATCH, SYSCALL_DISPATCH_FILTER_BLOCK, VIRTUAL_START, VIRTUAL_START, (long)NULL);
}
