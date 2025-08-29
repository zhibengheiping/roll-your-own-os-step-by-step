#include <stdio.h>
#include <unistd.h>
#include "signal.h"
#include "timer.h"

void
sig_handler(int signo, siginfo_t *info, void *context) {
  printf("TIMER\n");
  fflush(stdout);
}

int
main(void) {
  signal_set_handler(SIGALRM, sig_handler);

  struct itimerspec spec = {
    .it_value = { .tv_sec = 1, .tv_nsec = 0, },
    .it_interval = { .tv_sec = 1, .tv_nsec = 0, },
  };
  timer_start(&spec);

  for (;;) {
    printf("PAUSE\n");
    pause();
  }

  return 0;
}
