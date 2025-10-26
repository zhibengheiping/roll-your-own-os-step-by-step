#include <asm/bootparam.h>

struct setup_header setup __attribute__((section(".setup"), aligned(1))) = {
  .setup_sects = 1,
  .root_flags = 0,
  .header = 0,
  .version = 0,
};
