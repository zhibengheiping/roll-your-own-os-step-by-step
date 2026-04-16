#include <stdio.h>
#include <time.h>
#include "print-time.h"

void
print_time(void) {
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  printf("\r%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
  fflush(stdout);
}

int
__attribute__((weak))
main() {
  print_time();
  printf("\n");
  return 0;
}
