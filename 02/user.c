#include "osrt.h"
#include "syscall.h"
#include "elf.h"
#include "extent.h"
#include "paging.h"
#include "sched.h"
#include "tar.h"

struct task task_main = {0};

__attribute__((interrupt))
static
void
timer_handler(struct interrupt_frame *frame) {
  interrupt_eoi();
  task_yield();
}

#define ELF_BASE 0x100000
#define STACK_SIZE 0x4000

static
void *
load_elf(struct boot_info *info, char const *name) {
  Elf64_Ehdr *ehdr = tar_find(info->initrd, name);
  size_t size = elf_memsz(ehdr);
  paging_new_table();
  paging_alloc_pages(ELF_BASE, size);
  paging_alloc_pages(info->stack_top - STACK_SIZE, STACK_SIZE);
  paging_alloc_pages(info->stack_top/2 - STACK_SIZE, STACK_SIZE);
  void *addr = elf_load(ehdr, (void *)ELF_BASE);
  return addr;
}

void
kernel_main(struct boot_info *info) {
  syscall_init(info);
  elf_init();
  extent_init(info);

  sched_init(info);
  struct task task1, task2;
  task_init(&task1, load_elf(info, "solong.elf"), (void*)info->stack_top);
  task_init(&task2, load_elf(info, "thanks.elf"), (void*)info->stack_top);
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
