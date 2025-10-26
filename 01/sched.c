#include <stddef.h>
#include "sched.h"

static struct task *task_current;

void
sched_init(void) {
  task_current = &task_main;
}

void
task_init(struct task *task, void (*f)(void), void *stack_top) {
  task->jmpbuf[0] = 0;
  task->jmpbuf[1] = (uintptr_t)f;
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
