#pragma once
#include "../osrt.h"

#define KERNEL_STACK_TOP 0x800000000000ULL
void efi_loader_main(struct boot_info *info);
