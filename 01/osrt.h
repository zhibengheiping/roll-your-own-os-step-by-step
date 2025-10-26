#pragma once

#include <stdint.h>

struct interrupt_frame {
  uintptr_t rip, cs, rflags, rsp, ss;
};

void kernel_main(void);
void timer_init(void);
void timer_start(void);
void interrupt_init(void);
void interrupt_enable(void);
void interrupt_set_handler(unsigned char number, void (*handler)(struct interrupt_frame *));
void interrupt_eoi(void);
