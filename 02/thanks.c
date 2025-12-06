#include <unistd.h>
#include "osrt.h"

void
main(void) {
  static const char greet[] = "Thanks for all the fish\n";
  for (;;) {
    write(STDOUT_FILENO, greet, sizeof(greet));
    pause();
  }
}
