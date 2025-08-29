#include <stdio.h>
#include "interrupt.h"

void
main(void) {
  interrupt_enable();
  for (;;) {
    printf("So long\n");
    fflush(stdout);
    pause();
  }
}
