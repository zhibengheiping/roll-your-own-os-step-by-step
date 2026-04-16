#include <stddef.h>
#include <unistd.h>
#include <signal.h>
#include "print-time.h"

void
sighandler(int sig) {
}

int main() {
  struct sigaction sa = {
    .sa_handler = sighandler,
    .sa_flags   = 0
  };
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, NULL);

  for (;;) {
      alarm(1);
      pause();
      print_time();
  }
  return 0;
}
