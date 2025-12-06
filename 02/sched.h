#pragma once

#include <stdint.h>
#include "osrt.h"

struct task {
  uintptr_t jmpbuf[5];
  struct task *next;
  uintptr_t cr3;
};

extern struct task task_main;

void sched_init(struct boot_info *info);
void task_init(struct task *task, void (*f)(void), void *stack_top);
void task_yield(void);
