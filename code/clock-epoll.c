#include <sys/epoll.h>
#include "print-time.h"

int
main(void) {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  struct epoll_event e;

  for (;;) {
    epoll_wait(fd, &e, 1, 1000);
    print_time();
  }

  return 0;
}
