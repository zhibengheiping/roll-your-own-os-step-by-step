#include <unistd.h>
#include "user.h"

__attribute__((naked,noipa))
ssize_t
write(int fd, void const *buf, size_t count) {
  __asm__("syscall; ret" : : "a"(SYS_write), "D"(fd), "S"(buf), "d"(count) : "rcx", "r8", "r9", "r10", "r11", "memory");
}

__attribute__((naked,noipa))
int
pause(void) {
  __asm__("syscall; ret" : : "a"(SYS_pause) : "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r10", "r11", "memory");
}
