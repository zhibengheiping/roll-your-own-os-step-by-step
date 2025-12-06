#pragma once

#include <stdint.h>
#include <stddef.h>

struct interrupt_frame {
  uintptr_t rip, cs, rflags, rsp, ss;
};

struct boot_mem {
  char *base;
  size_t size;
};

struct boot_info {
  uintptr_t stack_top;
  uintptr_t virtual_start;
  void *initrd;
  size_t nmem;
  struct boot_mem *mem;
};

extern uintptr_t pml4[512];

void kernel_main(struct boot_info *info);
void timer_init(void);
void timer_start(void);
void interrupt_init(void);
void interrupt_enable(void);
void interrupt_disable(void);
void interrupt_set_handler(unsigned char number, void (*handler)(struct interrupt_frame *));
void interrupt_eoi(void);
void syscall_set_handler(void *handler);
