#pragma once

#include <stdint.h>

struct task {
  uintptr_t jmpbuf[5];
  struct task *next;
};

extern struct task task_main;

void sched_init(void);
void task_init(struct task *task, void (*f)(void), void *stack_top);
void task_yield(void);
