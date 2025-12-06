#pragma once

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 0x1000

void paging_new_table(void);
void paging_alloc_pages(uintptr_t base, size_t size);
