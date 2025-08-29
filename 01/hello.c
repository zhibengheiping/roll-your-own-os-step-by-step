#include <stdio.h>
#include <stdint.h>

void
hello(void) {
  uintptr_t a = 2;
  printf("Hello, world!\n");
}

int
main(void) {
  uintptr_t a = 1;
  hello();
  return 0;
}
