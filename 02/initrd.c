#include "osrt.h"
#include "elf.h"
#include "extent.h"
#include "sched.h"
#include "tar.h"

static char task1_stack[8192] __attribute__((aligned(4096))) = {0};
static char task2_stack[8192] __attribute__((aligned(4096))) = {0};

struct task task_main = {0};

__attribute__((interrupt))
static
void
timer_handler(struct interrupt_frame *frame) {
  interrupt_eoi();
  task_yield();
}

void *
load_elf(struct boot_info *info, char const *name) {
  Elf64_Ehdr *ehdr = tar_find(info->initrd, name);
  size_t size = elf_memsz(ehdr);
  char *base = extent_acquire(size);
  return elf_load(ehdr, base);
}

void
kernel_main(struct boot_info *info) {
  elf_init();
  extent_init(info);

  sched_init(info);
  struct task task1, task2;
  task_init(&task1, load_elf(info, "solong.so"), task1_stack + sizeof(task1_stack));
  task_init(&task2, load_elf(info, "thanks.so"), task2_stack + sizeof(task2_stack));
  task1.next = &task2;
  task2.next = &task1;
  task_main.next = &task1;

  interrupt_init();
  interrupt_set_handler(0x20, timer_handler);
  timer_init();
  timer_start();

  task_yield();
  return;
}
