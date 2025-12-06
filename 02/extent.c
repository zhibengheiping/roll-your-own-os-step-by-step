#include "extent.h"

static uintptr_t virtual_start;
static size_t nmem;
static struct boot_mem *mem;

void
extent_init(struct boot_info *info) {
  virtual_start = info->virtual_start;
  nmem = info->nmem;
  mem = info->mem;
}

void *
extent_acquire(size_t size) {
  for (size_t i=0; i<nmem; ++i) {
    if (mem[i].size < size)
      continue;
    void *base = mem[i].base;
    mem[i].base += size;
    mem[i].size -= size;
    return base;
  }
  return NULL;
}

uintptr_t
extent_phyaddr(void *ptr) {
  return ((uintptr_t)ptr) - virtual_start;
}

void *
extent_virtaddr(uintptr_t addr) {
  return (void *)(virtual_start + addr);
}
