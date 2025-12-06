#include <unistd.h>
#include "osrt.h"

void
main(void) {
  static const char greet[] = "So long\n";
  for (;;) {
    write(STDOUT_FILENO, greet, sizeof(greet));
    pause();
  }
}
