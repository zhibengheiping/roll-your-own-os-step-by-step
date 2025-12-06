#include <unistd.h>
#include <string.h>
#include "elf.h"


static struct {
  char const *name;
  void *addr;
} symbols[2];

void
elf_init(void) {
  symbols[0].name = "write";
  symbols[0].addr = write;
  symbols[1].name = "pause";
  symbols[1].addr = pause;
}

static
void *
lookup_symbol(char const *name) {
  for (size_t i=0; i<sizeof(symbols)/sizeof(symbols[0]); ++i) {
    if (strcmp(name, symbols[i].name))
      continue;
    return symbols[i].addr;
  }
  return NULL;
}

size_t
elf_memsz(Elf64_Ehdr *ehdr) {
  char *elf = (char *)ehdr;
  Elf64_Addr max_addr = 0;
  for (Elf64_Half i=0; i<ehdr->e_phnum; ++i) {
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf + ehdr->e_phoff + ehdr->e_phentsize * i);
    if (phdr->p_type != PT_LOAD)
      continue;
    Elf64_Addr addr = phdr->p_vaddr + phdr->p_memsz;
    addr = (addr + phdr->p_align - 1)/phdr->p_align * phdr->p_align;
    if (addr > max_addr)
      max_addr = addr;
  }
  return max_addr;
}

void *
elf_load(Elf64_Ehdr *ehdr, char *base) {
  char *elf = (char *)ehdr;
  for (Elf64_Half i=0; i<ehdr->e_phnum; ++i) {
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf + ehdr->e_phoff + ehdr->e_phentsize * i);
    if (phdr->p_type != PT_LOAD)
      continue;
    memcpy(base+phdr->p_vaddr, elf+phdr->p_offset, phdr->p_filesz);
  }

  char const *strtab;
  char const *symtab;
  Elf64_Xword syment;
  Elf64_Rela const* jmprel;
  Elf64_Xword pltrelsz = 0;

  for (Elf64_Half i=0; i<ehdr->e_phnum; ++i) {
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf + ehdr->e_phoff + ehdr->e_phentsize * i);
    if (phdr->p_type != PT_DYNAMIC)
      continue;
    for (Elf64_Dyn const *dynamic = (Elf64_Dyn const *)(base + phdr->p_vaddr); dynamic->d_tag != DT_NULL; ++dynamic)
      switch (dynamic->d_tag) {
      case DT_STRTAB:
        strtab = base + dynamic->d_un.d_ptr;
        break;
      case DT_SYMENT:
        syment = dynamic->d_un.d_val;
        break;
      case DT_SYMTAB:
        symtab = base + dynamic->d_un.d_ptr;
        break;
      case DT_JMPREL:
        jmprel = (Elf64_Rela const*)(base + dynamic->d_un.d_ptr);
        break;
      case DT_PLTRELSZ:
        pltrelsz = dynamic->d_un.d_val;
        break;
      default:
        break;
      }
    break;
  }

  for (Elf64_Xword i=0; i<pltrelsz/sizeof(Elf64_Rela); ++i) {
    Elf64_Rela const *rela = jmprel + i;
    Elf64_Sym const *sym = (Elf64_Sym const *)(symtab + syment * ELF64_R_SYM(rela->r_info));
    void **addr = (void **)(base + rela->r_offset);
    *addr = lookup_symbol(strtab + sym->st_name);
  }

  return base + ehdr->e_entry;
}
