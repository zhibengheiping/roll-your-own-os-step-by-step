#include <string.h>
#include "osrt.h"
#include "extent.h"
#include "paging.h"

#define PG_ADDR(x) ((x)&0xFFFFFFFFFFFFF000ULL)
#define PG_P 0x001
#define PG_W 0x002
#define PG_U 0x004
#define PG_PS 0x080

void
paging_new_table(void) {
  uintptr_t *table = extent_acquire(PAGE_SIZE);
  memcpy(table, pml4, PAGE_SIZE);
  __asm__("mov %0, %%cr3" : : "r"(extent_phyaddr(table)) : "memory");
}

static
void
alloc_page(uintptr_t *entry, uintptr_t addr) {
  char offsets[3] = {39, 30, 21};

  for (size_t i=0; i<sizeof(offsets); ++i) {
    uint16_t index = (addr >> offsets[i]) & 0x1FF;
    if (!(entry[index] & PG_P)) {
      void *page = extent_acquire(PAGE_SIZE);
      entry[index] = extent_phyaddr(page)|PG_P|PG_W|PG_U;
    }
    entry = extent_virtaddr(PG_ADDR(entry[index]));
  }

  uint16_t index = (addr >> 12) & 0x1FF;
  void *page = extent_acquire(PAGE_SIZE);
  entry[index] = extent_phyaddr(page)|PG_P|PG_W|PG_U;
}

void
paging_alloc_pages(uintptr_t base, size_t size) {
  uintptr_t table;
  __asm__("mov %%cr3, %0" : "=r"(table));

  for (size_t i=0; i<size; i+=PAGE_SIZE)
    alloc_page(extent_virtaddr(table), base+i);
}
