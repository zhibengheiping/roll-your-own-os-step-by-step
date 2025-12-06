#pragma once

#include "osrt.h"

void extent_init(struct boot_info *info);
void *extent_acquire(size_t size);
uintptr_t extent_phyaddr(void *ptr);
void *extent_virtaddr(uintptr_t addr);
