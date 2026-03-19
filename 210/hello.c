#include <unistd.h>

int
main(int argc, char const **argv) {
  static const char greet[] = "Hello, world!\n";
  write(STDOUT_FILENO, greet, sizeof(greet));
}

