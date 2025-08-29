#include <ucontext.h>
#include <stdatomic.h>
#include "interrupt.h"

uintptr_t interrupt_handler;
static volatile uintptr_t interrupt_request = 0;
static volatile uintptr_t interrupt_mask = 0;

void
interrupt_context_setup(void *context, uintptr_t handler) {
  ucontext_t *ucp = (ucontext_t *)context;
  uintptr_t *stack_top = (uintptr_t*)(ucp->uc_mcontext.gregs[REG_RSP] - 128);
  __asm__("mov %%ss, %0" : "=r"(*--stack_top));
  *--stack_top = ucp->uc_mcontext.gregs[REG_RSP];
  *--stack_top = ucp->uc_mcontext.gregs[REG_EFL];
  __asm__("mov %%cs, %0" : "=r"(*--stack_top));
  *--stack_top = ucp->uc_mcontext.gregs[REG_RIP];
  ucp->uc_mcontext.gregs[REG_RSP] = (uintptr_t)stack_top;
  ucp->uc_mcontext.gregs[REG_RIP] = (uintptr_t)handler;
}

__attribute__((noipa))
void
interrupt_call_handler(void) {
  asm goto (
     "mov %%rsp, -0x10(%%rsp);"
     "pushq $0x0;"
     "mov %%ss, 0x0(%%rsp);"
     "subq $0x8, %%rsp;"
     "pushfq;"
     "pushq $0x0;"
     "mov %%cs, 0x0(%%rsp);"
     "pushq $%l1;"
     "jmp *%0;"
     :
     : "r"(interrupt_handler)
     : "memory"
     : next
  );
next:
}

void
interrupt_enable(void) {
  if (!interrupt_mask)
    return;
  for (;;) {
    uintptr_t old = 1;
    while (atomic_compare_exchange_strong(&interrupt_request, &old, 0))
      interrupt_call_handler();
    old = 1;
    if (atomic_compare_exchange_strong(&interrupt_mask, &old, 0))
      return;
  }
}

__attribute__((interrupt))
void
interrupt_processor(struct interrupt_frame *frame) {
  interrupt_call_handler();
  interrupt_enable();
}

void
interrupt_sig_handler(int signo, siginfo_t *info, void *context) {
  if (interrupt_mask) {
    interrupt_request = 1;
  } else {
    interrupt_mask = 1;
    interrupt_context_setup(context, (uintptr_t)interrupt_processor);
  }
}
