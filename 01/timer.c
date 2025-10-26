#include <unistd.h>
#include "osrt.h"

__attribute__((interrupt))
static
void
timer_handler(struct interrupt_frame *frame) {
  interrupt_eoi();
}

void
kernel_main(void) {
  interrupt_init();
  interrupt_set_handler(0x20, timer_handler);
  timer_init();
  timer_start();
  interrupt_enable();
  for (;;) {
    static const char greet[] = "Hello, world!\n";
    write(STDOUT_FILENO, greet, sizeof(greet));
    pause();
  }
}
