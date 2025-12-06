#include <stdint.h>
#include "../osrt.h"
#include "efi.h"
#include "gdt.h"

struct segment {
  uintptr_t limit_lo: 16;
  uintptr_t base_lo: 24;
  uintptr_t type: 4;
  uintptr_t s: 1;
  uintptr_t dpl: 2;
  uintptr_t p: 1;
  uintptr_t limit_hi: 4;
  uintptr_t avl: 1;
  uintptr_t l: 1;
  uintptr_t db: 1;
  uintptr_t g: 1;
  uintptr_t base_hi: 8;
};

static struct segment gdt[8] = {
  [0] = {0},
  [1] = {
    .limit_lo = 0xFFFF,
    .limit_hi = 0xF,
    .base_lo = 0xFFFFFF,
    .base_hi = 0xFF,
    .type = 0xb,
    .s = 1,
    .dpl = 0,
    .p = 1,
    .avl = 0,
    .l = 1,
    .db = 0,
    .g = 1,
  },
  [2] = {
    .limit_lo = 0xFFFF,
    .limit_hi = 0xF,
    .base_lo = 0xFFFFFF,
    .base_hi = 0xFF,
    .type = 0x3,
    .s = 1,
    .dpl = 0,
    .p = 1,
    .avl = 0,
    .l = 1,
    .db = 0,
    .g = 1,
  },
  [3] = {
    .limit_lo = 0xFFFF,
    .limit_hi = 0xF,
    .base_lo = 0xFFFFFF,
    .base_hi = 0xFF,
    .type = 0xb,
    .s = 1,
    .dpl = 3,
    .p = 1,
    .avl = 0,
    .l = 0,
    .db = 1,
    .g = 1,
  },
  [4] = {
    .limit_lo = 0xFFFF,
    .limit_hi = 0xF,
    .base_lo = 0xFFFFFF,
    .base_hi = 0xFF,
    .type = 0x3,
    .s = 1,
    .dpl = 3,
    .p = 1,
    .avl = 0,
    .l = 1,
    .db = 0,
    .g = 1,
  },
  [5] = {
    .limit_lo = 0xFFFF,
    .limit_hi = 0xF,
    .base_lo = 0xFFFFFF,
    .base_hi = 0xFF,
    .type = 0xb,
    .s = 1,
    .dpl = 3,
    .p = 1,
    .avl = 0,
    .l = 1,
    .db = 0,
    .g = 1,
  },
  [6] = {
    .limit_lo = 0xFFFF,
    .limit_hi = 0xF,
    .base_lo = 0xFFFFFF,
    .base_hi = 0xFF,
    .type = 0x9,
    .s = 0,
    .dpl = 0,
    .p = 1,
    .avl = 0,
    .l = 1,
    .db = 0,
    .g = 0,
  },
};

struct gdtr {
  uint16_t limit;
  struct segment *base;
} __attribute__((packed));

static struct gdtr gdtr __attribute__((aligned(16))) = {0};

struct tss {
  uintptr_t _0: 32;
  uintptr_t rsp0, rsp1, rsp2;
  uintptr_t _1;
  uintptr_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
  uintptr_t _2;
  uintptr_t _3: 16;
  uintptr_t io_base: 16;
};

static struct tss tss = {
  .rsp0 = KERNEL_STACK_TOP,
  .io_base = 0xFFFF,
};


#define KERN_CS   0x08
#define KERN_SS   0x10
#define USER_CS32 0x18
#define USER_SS   0x20
#define USER_CS64 0x28
#define TSS       0x30

void
gdt_init(void) {
  gdtr.base = gdt;
  gdtr.limit = sizeof(gdt)-1;

  __asm__("lgdt %0" : : "m"(gdtr));
  __asm__("pushq $%c0\n"
          "pushq %1\n"
          "lretq\n"
          : : "i"(KERN_CS), "r"(&&next) : "memory");
 next:
  __asm__("mov %w0, %%ss; mov %w0, %%ds; mov %w0, %%es" : : "r"(KERN_SS));
  __asm__("mov %w0, %%fs; mov %w0, %%gs" : : "r"(0x0));

  uintptr_t base = (uintptr_t)&tss;
  uintptr_t limit = sizeof(tss);
  gdt[6].limit_lo = (limit & 0xFFFF);
  gdt[6].limit_hi = (limit >> 16) & 0xF;
  gdt[6].base_lo = (base & 0xFFFFFF);
  gdt[6].base_hi = (base >> 24) &0xFF;
  *(uintptr_t *)(gdt + 7) = (base >> 32);
}


#define RF_IF 0x200
#define EFER    0xC0000080
#define STAR    0xC0000081
#define LSTAR   0xC0000082
#define SFMASK  0xC0000084


static
inline
void
wrmsr(uint32_t msr, uint64_t value) {
  __asm__("wrmsr" : : "c"(msr), "a"(value&0xFFFFFFFF), "d"((value>>32)&0xFFFFFFFF));
}

static
inline
uint64_t
rdmsr(uint32_t msr) {
  union {
    struct {
      uint32_t eax;
      uint32_t edx;
    };
    uint64_t value;
  } result;
  __asm__("rdmsr" : "=a"(result.eax), "=d"(result.edx) : "c"(msr));
  return result.value;
}

void
syscall_set_handler(void *handler) {
  __asm__ ("ltr %w0" : : "r"(TSS));
  uint64_t efer = rdmsr(EFER);
  wrmsr(EFER, efer|1);
  uint64_t star_val = ((uint64_t)USER_CS32) << 48  | ((uint64_t)KERN_CS) << 32;
  wrmsr(STAR, star_val);
  wrmsr(LSTAR, (uintptr_t)handler);
  wrmsr(SFMASK, RF_IF);
}
