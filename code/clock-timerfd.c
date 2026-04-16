#include <stdint.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include "print-time.h"

int
main(void) {
  struct itimerspec its = {
    .it_interval = {1, 0},
    .it_value    = {1, 0}
  };
  int fd = timerfd_create(CLOCK_REALTIME, 0);
  timerfd_settime(fd, 0, &its, NULL);

  for (;;) {
    uint64_t e;
    read(fd, &e, sizeof(e));
    print_time();
  }

  return 0;
}

