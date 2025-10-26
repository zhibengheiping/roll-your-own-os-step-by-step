#include <sys/io.h>
#include "../osrt.h"
#include "pic.h"

#define PIT_CMD_MODE2 0x04
#define PIT_CMD_LOBYTE 0x10
#define PIT_CMD_HIBYTE 0x20

#define PIT_CMD_SC0 0x00
#define PIT_CMD_SC1 0x40
#define PIT_CMD_SC2 0x80

#define PIT_PORT_CMD 0x43

void
timer_init(void) {
  pic_clear_mask(0);
  outb(PIT_CMD_SC0|PIT_CMD_LOBYTE|PIT_CMD_HIBYTE|PIT_CMD_MODE2, PIT_PORT_CMD);
}

void
timer_start(void) {
  outb(0xFF, PIT_PORT_CMD);
  outb(0xFF, PIT_PORT_CMD);
}
