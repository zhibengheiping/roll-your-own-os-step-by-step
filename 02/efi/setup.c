#include <asm/bootparam.h>

struct setup_header setup __attribute__((section(".setup"), aligned(1))) = {
  .setup_sects = 1,
  .root_flags = 0,
  .header = 0x53726448,
  .version = 0x20f,
  .initrd_addr_max = 0xFFFFFFFF,
};
