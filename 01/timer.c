#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "timer.h"

void
timer_start(struct itimerspec *spec) {
  timer_t timer;
  struct sigevent sigev = {
    .sigev_notify = SIGEV_SIGNAL,
    .sigev_signo = SIGALRM,
  };

  if (timer_create(CLOCK_REALTIME, &sigev, &timer) == -1) {
    perror("timer_create");
    exit(EXIT_FAILURE);
  }

  if (timer_settime(timer, 0, spec, NULL) == -1) {
    perror("timer_settime");
    exit(EXIT_FAILURE);
  }
}
