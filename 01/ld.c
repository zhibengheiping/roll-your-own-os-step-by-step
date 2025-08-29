#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <elf.h>

#include "timer.h"
#include "signal.h"
#include "task.h"
#include "interrupt.h"

__attribute__((interrupt))
void
alarm_handler(struct interrupt_frame *frame) {
  task_yield();
}

void *
elf_load(char const *name) {
  Elf64_Ehdr ehdr;
  int fd = open(name, O_RDONLY);
  if (read(fd, &ehdr, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
    perror("read");
    exit(EXIT_FAILURE);
  }

  Elf64_Phdr phdr[ehdr.e_phnum];
  Elf64_Addr max_addr = 0;

  for (Elf64_Half i=0; i<ehdr.e_phnum; ++i) {
    if (pread(fd, &phdr[i], sizeof(Elf64_Phdr), ehdr.e_phoff + ehdr.e_phentsize * i) != sizeof(Elf64_Phdr)) {
      perror("pread");
      exit(EXIT_FAILURE);
    }

    if (phdr[i].p_type == PT_LOAD) {
      Elf64_Addr addr = phdr[i].p_vaddr + phdr[i].p_memsz;
      addr = (addr + 1) / phdr[i].p_align * phdr[i].p_align;
      if (addr > max_addr)
        max_addr = addr;
    }
  }

  char *base = mmap(NULL, max_addr, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (base == MAP_FAILED) {
      perror("pread");
      exit(EXIT_FAILURE);
  }
  char const *strtab;
  char const *symtab;
  Elf64_Xword syment;
  char const *rela_base;
  Elf64_Xword relasz;
  Elf64_Xword relaent;

  for (Elf64_Half i=0; i<ehdr.e_phnum; ++i) {
    switch (phdr[i].p_type) {
    case PT_LOAD:
      if (pread(fd, base + phdr[i].p_vaddr, phdr[i].p_filesz, phdr[i].p_offset) != phdr[i].p_filesz) {
        perror("pread");
        exit(EXIT_FAILURE);
      }
      break;
    case PT_DYNAMIC:
      for (Elf64_Dyn const *dynamic = (Elf64_Dyn const *)(base + phdr[i].p_vaddr); dynamic->d_tag != DT_NULL; ++dynamic) {
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
        case DT_RELA:
          rela_base = base + dynamic->d_un.d_ptr;
          break;
        case DT_RELASZ:
          relasz = dynamic->d_un.d_val;
          break;
        case DT_RELAENT:
          relaent = dynamic->d_un.d_val;
          break;
        }
      }
      break;
    }
  }

  close(fd);

  for (Elf64_Xword i=0; i<relasz/relaent; ++i) {
    Elf64_Rela const *rela = (Elf64_Rela const *)(rela_base + relaent * i);

    if (ELF64_R_TYPE(rela->r_info) != R_X86_64_GLOB_DAT) {
        fprintf(stderr, "unsupported relacation type %llx\n", ELF64_R_TYPE(rela->r_info));
        exit(EXIT_FAILURE);
    }

    Elf64_Sym const *sym = (Elf64_Sym const *)(symtab + syment * ELF64_R_SYM(rela->r_info));
    uintptr_t *addr = (uintptr_t *)(base + rela->r_offset);
    if (sym->st_shndx != SHN_UNDEF) {
        fprintf(stderr, "symbol is not UNDEF\n");
        exit(EXIT_FAILURE);
    }
    *addr = (uintptr_t)dlsym(NULL, strtab + sym->st_name);
  }

  for (Elf64_Half i=0; i<ehdr.e_phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD)
      continue;
    Elf64_Addr addr = phdr[i].p_vaddr;
    addr = addr / phdr[i].p_align * phdr[i].p_align;
    int prot = 0;
    if (phdr[i].p_flags & PF_R)
      prot |= PROT_READ;
    if (phdr[i].p_flags & PF_W)
      prot |= PROT_WRITE;
    if (phdr[i].p_flags & PF_X)
      prot |= PROT_EXEC;

    if (mprotect(base + addr, phdr[i].p_memsz, prot) != 0) {
        perror("pread");
        exit(EXIT_FAILURE);
    }
  }

  return base + ehdr.e_entry;
}

int
main(int argc, char const **argv) {
  interrupt_handler = (uintptr_t)alarm_handler;
  signal_set_handler(SIGALRM, interrupt_sig_handler);

  struct itimerspec spec1 = {
    .it_value = { .tv_sec = 1, .tv_nsec = 0, },
    .it_interval = { .tv_sec = 1, .tv_nsec = 0, },
  };
  struct itimerspec spec2 = {
    .it_value = { .tv_sec = 1, .tv_nsec = 0, },
    .it_interval = { .tv_sec = 0, .tv_nsec = 0, },
  };
  timer_start(&spec1);
  timer_start(&spec2);

  struct task task1, task2;
  task_init(&task1, elf_load(argv[1]), 8192);
  task_init(&task2, elf_load(argv[2]), 8192);
  task1.next = &task2;
  task2.next = &task1;
  task_main.next = &task1;
  task_yield();
  return 0;
}
