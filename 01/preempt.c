#include <stdio.h>
#include "task.h"
#include "signal.h"
#include "timer.h"
#include "interrupt.h"

void
solong(void) {
  for (;;) {
    printf("So long\n");
    fflush(stdout);
    pause();
  }
}

void
thanks(void) {
  for (;;) {
    printf("Thanks for all the fish\n");
    fflush(stdout);
    pause();
  }
}

__attribute__((interrupt))
void
alarm_handler(struct interrupt_frame *frame) {
  task_yield();
}

void
sig_handler(int signo, siginfo_t *info, void *context) {
  interrupt_context_setup(context, (uintptr_t)alarm_handler);
}

int
main() {
  signal_set_handler(SIGALRM, sig_handler);

  struct itimerspec spec = {
    .it_value = { .tv_sec = 1, .tv_nsec = 0, },
    .it_interval = { .tv_sec = 1, .tv_nsec = 0, },
  };
  timer_start(&spec);

  struct task task1, task2;
  task_init(&task1, solong, 8192);
  task_init(&task2, thanks, 8192);
  task1.next = &task2;
  task2.next = &task1;
  task_main.next = &task1;
  task_yield();
  return 0;
}
