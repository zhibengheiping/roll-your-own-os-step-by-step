#include <sys/io.h>
#include "pic.h"

#define PIC0_PORT_BASE 0x20
#define PIC1_PORT_BASE 0xA0
#define PIC_PORT_A0 1

#define PIC_ICW1 0x10
#define PIC_ICW1_IC4 0x1
#define PIC_ICW4_8086 0x1

#define PIC_OCW2 0
#define PIC_OCW2_EOI 0x20
#define PIC_OCW3 0x08
#define PIC_OCW3_RR 0x02
#define PIC_OCW3_RIS 0x01


static
unsigned char
pic_read_imr(unsigned char port) {
  return inb(port|PIC_PORT_A0);
}

static
void
pic_write_imr(unsigned char mask, unsigned char port) {
  outb(mask, port|PIC_PORT_A0);
}

static
unsigned char
pic_read_isr(unsigned char port) {
  outb(PIC_OCW3|PIC_OCW3_RR|PIC_OCW3_RIS, port);
  return inb(port);
}

static
void
pic_issue_eoi(unsigned char port) {
  outb(PIC_OCW2|PIC_OCW2_EOI, port);
}

void
pic_eoi(void) {
  unsigned char isr = pic_read_isr(PIC0_PORT_BASE);
  if (isr & 0x07 == 0x04) {
    pic_issue_eoi(PIC1_PORT_BASE);
    if (pic_read_isr(PIC1_PORT_BASE) != 0)
      return;
  }
  pic_issue_eoi(PIC0_PORT_BASE);
}

void
pic_clear_mask(unsigned char irq) {
  unsigned char port = (irq<8)?PIC0_PORT_BASE:PIC1_PORT_BASE;
  unsigned char mask = pic_read_imr(port);
  mask ^= mask & (unsigned char)(1 << (irq % 8));
  pic_write_imr(mask, port);
}

void
pic_init(void) {
  outb(PIC_ICW1|PIC_ICW1_IC4, PIC0_PORT_BASE);
  outb(PIC_ICW1|PIC_ICW1_IC4, PIC1_PORT_BASE);
  // ICW2
  outb(0x20, PIC0_PORT_BASE|PIC_PORT_A0);
  outb(0x28, PIC1_PORT_BASE|PIC_PORT_A0);
  // ICW3
  outb(1<<2, PIC0_PORT_BASE|PIC_PORT_A0);
  outb(2, PIC1_PORT_BASE|PIC_PORT_A0);
  // ICW4
  outb(PIC_ICW4_8086, PIC0_PORT_BASE|PIC_PORT_A0);
  outb(PIC_ICW4_8086, PIC1_PORT_BASE|PIC_PORT_A0);

  pic_write_imr(0xff, PIC0_PORT_BASE);
  pic_write_imr(0xff, PIC1_PORT_BASE);
  pic_clear_mask(2);
}
