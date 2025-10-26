#include <unistd.h>
#include "sched.h"
#include "osrt.h"

void
solong(void) {
  interrupt_enable();
  static const char greet[] = "So long\n";
  for (;;) {
    write(STDOUT_FILENO, greet, sizeof(greet));
    pause();
  }
}

void
thanks(void) {
  interrupt_enable();
  static const char greet[] = "Thanks for all the fish\n";
  for (;;) {
    write(STDOUT_FILENO, greet, sizeof(greet));
    pause();
  }
}

static char task1_stack[8192] __attribute__((aligned(4096))) = {0};
static char task2_stack[8192] __attribute__((aligned(4096))) = {0};

struct task task_main;

__attribute__((interrupt))
static
void
timer_handler(struct interrupt_frame *frame) {
  interrupt_eoi();
  task_yield();
}

void
kernel_main(void) {
  sched_init();
  struct task task1, task2;
  task_init(&task1, solong, task1_stack + sizeof(task1_stack));
  task_init(&task2, thanks, task2_stack + sizeof(task2_stack));
  task1.next = &task2;
  task2.next = &task1;
  task_main.next = &task1;

  interrupt_init();
  interrupt_set_handler(0x20, timer_handler);
  timer_init();
  timer_start();

  task_yield();
}
