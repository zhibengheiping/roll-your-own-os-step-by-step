#include "../osrt.h"
#include "idt.h"

struct gate {
  uintptr_t offset_lo: 16;
  uintptr_t segment_selector: 16;
  uintptr_t ist: 3;
  uintptr_t zero: 5;
  uintptr_t type: 3;
  uintptr_t size: 1;
  uintptr_t s: 1;
  uintptr_t dpl: 2;
  uintptr_t p: 1;
  uintptr_t offset_mid: 16;
  uintptr_t offset_hi: 32;
  uintptr_t reserved: 32;
} __attribute__((packed));

static struct gate idt[256] __attribute__((aligned(4096))) = {0};

struct idtr {
  uint16_t limit;
  struct gate *base;
} __attribute__((packed));

void
idt_init(void) {
  struct idtr idtr = {
    .base = idt,
    .limit = sizeof(idt) - 1,
  };
  __asm__("lidt %0" : : "m"(idtr));
}


void
interrupt_set_handler(unsigned char number, void (*handler)(struct interrupt_frame *)) {
  struct idtr idtr;
  __asm__("sidt %0" : : "m"(idtr));
  uint16_t cs;
  __asm__("mov %%cs, %0" : "=r"(cs));

  uintptr_t offset = (uintptr_t)handler;

  struct gate gate = {
    .offset_lo = offset & 0xFFFF,
    .segment_selector = cs,
    .ist = 0,
    .zero = 0,
    .type = 0x6,
    .size = 1,
    .s = 0,
    .dpl = 0,
    .p = 1,
    .offset_mid = (offset >> 16) & 0xFFFF,
    .offset_hi = (offset >> 32) & 0xFFFFFFFF,
    .reserved = 0,
  };

  idtr.base[number] = gate;
}
