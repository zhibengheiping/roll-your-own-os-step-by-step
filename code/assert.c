#include <assert.h>

void
fire(void) {
  assert(0);
}

int
main(void) {
  fire();
  return 1;
}
