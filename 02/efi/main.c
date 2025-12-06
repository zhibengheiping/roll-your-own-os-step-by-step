#include <unistd.h>
#include "efi.h"
#include "idt.h"
#include "gdt.h"
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
interrupt_disable(void) {
  __asm__("cli");
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

void
efi_loader_main(struct boot_info *info) {
  gdt_init();
  idt_init();
  pml4[0] = 0;
  __asm__("mov %0, %%cr3": :"r"(((uintptr_t)pml4) - info->virtual_start) : "memory");
  kernel_main(info);
}
