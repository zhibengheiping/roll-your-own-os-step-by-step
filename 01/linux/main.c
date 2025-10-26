#include <unistd.h>
#include "../osrt.h"
#include "signal.h"

int
main(void) {
  static const char greet[] = "\x1b[2J\x1b[H";
  write(STDOUT_FILENO, greet, sizeof(greet));
  signal_init();
  kernel_main();
  return 0;
}
