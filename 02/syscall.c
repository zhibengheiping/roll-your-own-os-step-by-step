#include <unistd.h>
#include "osrt.h"
#include "syscall.h"

static void *syscalls[SYS_MAX];
static uintptr_t kernel_stack_top;

__attribute__((naked,noipa))
static
void
syscall_handler(void) {
  __asm__("mov %%rsp, %%r9\n"
          "mov %0, %%rsp\n"
          "push %%r9\n"
          "push %%rcx\n"
          "push %%r11\n"
          "mov %%r10, %%rcx\n"
          "lea %1, %%r10\n"
          "call *(%%r10,%%rax,8)\n"
          "pop %%r11\n"
          "pop %%rcx\n"
          "pop %%rsp\n"
          "sysretq"
          :
          : "m"(kernel_stack_top), "m"(syscalls)
          : "rax", "rcx", "r9", "r10", "r11", "memory");
}

int
sys_pause(void) {
  interrupt_enable();
  int result = pause();
  interrupt_disable();
  return result;
}

void
syscall_init(struct boot_info *info) {
  kernel_stack_top = info->stack_top;
  syscall_set_handler(syscall_handler);
  syscalls[SYS_write] = write;
  syscalls[SYS_pause] = sys_pause;
}
