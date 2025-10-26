#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../osrt.h"
#include "signal.h"
#include "interrupt.h"

static timer_t timer;

static
void
sig_handler(int signo, siginfo_t *info, void *context) {
  interrupt_request(0x20, context);
}

void
timer_init(void) {
  struct sigevent sigev = {
    .sigev_notify = SIGEV_SIGNAL,
    .sigev_signo = SIGALRM,
  };
  if (timer_create(CLOCK_REALTIME, &sigev, &timer) == -1) {
    perror("timer_create");
    exit(EXIT_FAILURE);
  }

  signal_set_handler(SIGALRM, sig_handler);
}

void
timer_start(void) {
  long nsec = (0xFFFFL * 1000000000L) / 1193182L;

  struct itimerspec spec = {
    .it_value = { .tv_sec = 0, .tv_nsec = nsec, },
    .it_interval = { .tv_sec = 0, .tv_nsec = nsec, },
  };

  if (timer_settime(timer, 0, &spec, NULL) == -1) {
    perror("timer_settime");
    exit(EXIT_FAILURE);
  }
}
