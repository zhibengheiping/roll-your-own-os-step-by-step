#include <ucontext.h>
#include "../osrt.h"
#include "interrupt.h"

static uintptr_t handlers[256] = {0};

void
interrupt_set_handler(unsigned char number, void (*handler)(struct interrupt_frame *)) {
  handlers[number] = (uintptr_t)handler;
}

void
interrupt_request(unsigned char number, void *context) {
  ucontext_t *ucp = (ucontext_t *)context;
  uintptr_t *stack_top = (uintptr_t*)(ucp->uc_mcontext.gregs[REG_RSP] - 128);
  __asm__("mov %%ss, %0" : "=r"(*--stack_top));
  *--stack_top = ucp->uc_mcontext.gregs[REG_RSP];
  *--stack_top = ucp->uc_mcontext.gregs[REG_EFL];
  __asm__("mov %%cs, %0" : "=r"(*--stack_top));
  *--stack_top = ucp->uc_mcontext.gregs[REG_RIP];
  ucp->uc_mcontext.gregs[REG_RSP] = (uintptr_t)stack_top;
  ucp->uc_mcontext.gregs[REG_RIP] = handlers[number];
}

void
interrupt_init(void) {
}

void
interrupt_enable(void) {
}

void
interrupt_eoi(void) {
}

