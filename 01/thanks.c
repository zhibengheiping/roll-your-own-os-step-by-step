#include <stdio.h>
#include "interrupt.h"

void
main(void) {
  interrupt_enable();
  for (;;) {
    printf("Thanks for all the fish\n");
    fflush(stdout);
    pause();
  }
}
