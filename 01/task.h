#pragma once

#include <stdint.h>

struct task {
  uintptr_t jmpbuf[5];
  struct task *next;
};

extern struct task task_main;
extern struct task *task_current;

void task_init(struct task *task, void (*f)(void), size_t stack_size);
void task_yield(void);
