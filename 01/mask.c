#include <stdio.h>

#include "timer.h"
#include "signal.h"
#include "task.h"
#include "interrupt.h"

__attribute__((interrupt))
void
alarm_handler(struct interrupt_frame *frame) {
  task_yield();
}

void
solong(void) {
  interrupt_enable();
  for (;;) {
    printf("So long\n");
    fflush(stdout);
    pause();
  }
}

void
thanks(void) {
  interrupt_enable();
  for (;;) {
    printf("Thanks for all the fish\n");
    fflush(stdout);
    pause();
  }
}

int
main() {
  interrupt_handler = (uintptr_t)alarm_handler;
  signal_set_handler(SIGALRM, interrupt_sig_handler);

  struct itimerspec spec1 = {
    .it_value = { .tv_sec = 1, .tv_nsec = 0, },
    .it_interval = { .tv_sec = 1, .tv_nsec = 0, },
  };
  struct itimerspec spec2 = {
    .it_value = { .tv_sec = 1, .tv_nsec = 0, },
    .it_interval = { .tv_sec = 0, .tv_nsec = 0, },
  };
  timer_start(&spec1);
  timer_start(&spec2);

  struct task task1, task2;
  task_init(&task1, solong, 8192);
  task_init(&task2, thanks, 8192);
  task1.next = &task2;
  task2.next = &task1;
  task_main.next = &task1;
  task_yield();
  return 0;
}
