#include <stddef.h>
#include <sys/mman.h>
#include "task.h"

struct task task_main;
struct task *task_current = &task_main;

void
task_init(struct task *task, void (*f)(void), size_t stack_size) {
  void *addr = mmap(NULL, stack_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
  task->jmpbuf[0] = 0;
  task->jmpbuf[1] = (uintptr_t)f;
  task->jmpbuf[2] = (uintptr_t)addr + stack_size;
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
