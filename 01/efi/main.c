#include <unistd.h>
#include "../osrt.h"
#include "pic.h"

void
interrupt_init(void) {
  pic_init();
}

void
interrupt_enable(void) {
  __asm__("sti");
}

void
interrupt_eoi(void) {
  pic_eoi();
}

int
pause(void) {
  __asm__("hlt");
  return -1;
}
