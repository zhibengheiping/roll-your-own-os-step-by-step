#include <stddef.h>
#include "osrt.h"
#include "sched.h"

static struct task *task_current;

void
sched_init(struct boot_info *info) {
  task_current = &task_main;
}

static
__attribute__((naked))
void
start() {
  interrupt_enable();
  __asm__("call *%rbp");
}

void
task_init(struct task *task, void (*f)(void), void *stack_top) {
  task->jmpbuf[0] = (uintptr_t)f;
  task->jmpbuf[1] = (uintptr_t)start;
  task->jmpbuf[2] = (uintptr_t)stack_top;
}

static
__attribute__((naked,noipa))
void
task_resume() {
  __builtin_longjmp(task_current->jmpbuf, 1);
}

void
task_yield(void) {
  if (__builtin_setjmp(task_current->jmpbuf))
    return;

  task_current = task_current->next;
  task_resume();
}
