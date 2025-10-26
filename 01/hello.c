#include <unistd.h>
#include "osrt.h"

void
kernel_main(void) {
  static const char greet[] = "Hello, world!\n";
  write(STDOUT_FILENO, greet, sizeof(greet));
}

