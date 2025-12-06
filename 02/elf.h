#pragma once

#include <stddef.h>
#include <elf.h>

void elf_init(void);
size_t elf_memsz(Elf64_Ehdr *ehdr);
void *elf_load(Elf64_Ehdr *ehdr, char *base);
