#pragma once

#include <stdint.h>
#include <signal.h>

struct interrupt_frame {
  uintptr_t rip, cs, rflags, rsp, ss;
};

void interrupt_context_setup(void *context, uintptr_t handler);
void interrupt_sig_handler(int signo, siginfo_t *info, void *context);
extern uintptr_t interrupt_handler;
void interrupt_enable(void);
