#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <sys/syscall.h>
#include <sys/mman.h>

void
exit(int code) {
    asm volatile (
        "syscall"
        :
        : "a"(SYS_exit), "D"(code)
        : "rcx", "r11", "memory"
    );
    __builtin_unreachable();
}

int
close(int fd) {
  asm volatile (
      "syscall"
      : "=a"(fd)
      : "a"(SYS_close), "D"(fd)
      : "rcx", "r11", "memory"
  );
  return fd;
}

int
open(const char *path, int flags, ...) {
  int fd;
  asm volatile (
      "syscall"
      : "=a"(fd)
      : "a"(SYS_open), "D"(path), "S"(flags)
      : "rcx", "r11", "memory"
  );
  return fd;
}

ssize_t
pread(int fd, void *buf, size_t count, off_t offset) {
  int size;
  asm volatile (
      "mov %5, %%r10;"
      "syscall"
      : "=a"(size)
      : "a"(SYS_pread64), "D"(fd), "S"(buf), "d"(count), "r"(offset)
      : "rcx", "r11", "memory"
  );
  return size;
}

void *
mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
  asm volatile (
      "mov %5, %%r10;"
      "mov %6, %%r8;"
      "mov %7, %%r9;"
      "syscall"
      : "=a"(addr)
      : "a"(SYS_mmap), "D"(addr), "S"(length), "d"(prot), "r"((int64_t)flags), "r"((int64_t)fd), "r"(offset)
      : "rcx", "r8", "r9", "r10", "r11", "memory"
  );
  return addr;
}

Elf64_Addr
align_down(Elf64_Addr addr, Elf64_Xword align) {
  return addr - addr % align;
}

Elf64_Addr
align_up(Elf64_Addr addr, Elf64_Xword align) {
  return align_down(addr + align - 1, align);
}

char *
load_elf(char const *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    exit(1);

  Elf64_Ehdr ehdr;
  if (pread(fd, &ehdr, sizeof(Elf64_Ehdr), 0) < 0)
    exit(1);

  char buf[ehdr.e_phentsize * ehdr.e_phnum];
  if (pread(fd, buf, sizeof(buf), ehdr.e_phoff) < 0)
    exit(1);

  Elf64_Addr maxaddr = 0;

  for (Elf64_Half i=0; i<ehdr.e_phnum; ++i) {
    Elf64_Phdr *phdr = (Elf64_Phdr *)(buf + ehdr.e_phentsize * i);
    if (phdr->p_type != PT_LOAD)
      continue;
    Elf64_Addr addr = align_up(phdr->p_vaddr + phdr->p_memsz, phdr->p_align);
    if (addr > maxaddr)
      maxaddr = addr;
  }

  char *base = mmap(NULL, maxaddr, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (base == (char*)MAP_FAILED)
    exit(1);

  for (Elf64_Half i=0; i<ehdr.e_phnum; ++i) {
    Elf64_Phdr *phdr = (Elf64_Phdr *)(buf + ehdr.e_phentsize * i);
    if (phdr->p_type != PT_LOAD)
      continue;
    Elf64_Addr lo = align_down(phdr->p_vaddr, phdr->p_align);
    Elf64_Addr hi = align_up(phdr->p_vaddr + phdr->p_memsz, phdr->p_align);

    int prot = 0;
    if (phdr->p_flags & PF_R)
      prot |= PROT_READ;
    if (phdr->p_flags & PF_W)
      prot |= PROT_WRITE;
    if (phdr->p_flags & PF_X)
      prot |= PROT_EXEC;

    if (prot & PROT_WRITE) {
      if (mmap(base + lo, hi - lo, prot, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) != base + lo)
        exit(1);
      if (pread(fd, base + phdr->p_vaddr, phdr->p_filesz, phdr->p_offset) < 0)
        exit(1);
    } else {
      if (mmap(base + lo, hi - lo, prot, MAP_PRIVATE|MAP_FIXED, fd, lo) != base + lo)
        exit(1);
    }
  }

  close(fd);

  return base;
}

void *
find_entry(int argc, char **argv) {
  char **envp = argv + argc + 1;
  while (*envp) envp++;
  Elf64_auxv_t *auxv = (Elf64_auxv_t *)(envp + 1);

  char *elf = load_elf(argv[0]);
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf;
  char *phdr = elf + ehdr->e_phoff;
  char *interp = NULL;
  for (Elf64_Half i=0; i<ehdr->e_phnum; ++i) {
    Elf64_Phdr *p = (Elf64_Phdr *)(phdr + ehdr->e_phentsize * i);
    if (p->p_type == PT_INTERP)
      interp = elf + p->p_vaddr;
  }
  if (interp != NULL)
    interp = load_elf(interp);

  for (; auxv->a_type != AT_NULL; auxv++) {
    switch (auxv->a_type) {
    case AT_BASE:
      auxv->a_un.a_val = (uint64_t)interp;
      break;
    case AT_ENTRY:
      auxv->a_un.a_val = (uint64_t)(elf + ehdr->e_entry);
      break;
    case AT_PHDR:
      auxv->a_un.a_val = (uint64_t)phdr;
      break;
    case AT_PHENT:
      auxv->a_un.a_val = (uint64_t)ehdr->e_phentsize;
      break;
    case AT_PHNUM:
      auxv->a_un.a_val = (uint64_t)ehdr->e_phnum;
      break;
    case AT_EXECFN:
      auxv->a_un.a_val = (uint64_t)argv[0];
      break;
    }
  }

  interp = (interp)?interp:elf;

  void *entry = interp + ((Elf64_Ehdr *)interp)->e_entry;
  return entry;
}

__attribute__((naked, noipa))
void
_start(void) {
  long *sp;
  asm volatile ("mov %%rsp, %0" : "=r"(sp));
  long argc = sp[0] - 2;
  sp[2] = argc;
  void *entry = find_entry(argc, (char **)(sp+3));

  asm volatile (
      "mov %0, %%rsp;"
      "xor %%rdx, %%rdx;"
      "jmp *%1"
      :
      : "r"(sp+2), "r"(entry)
      : "rdx", "rdi", "rax", "memory");
}
