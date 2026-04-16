#include <unistd.h>
#include "print-time.h"

int
main(void) {
  for (;;) {
    sleep(1);
    print_time();
  }
  return 0;
}
