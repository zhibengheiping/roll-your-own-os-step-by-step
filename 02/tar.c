#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include "tar.h"

static
size_t
decode_oct(char const *s) {
  size_t n = 0;
  for ( ; ('0' <= *s) && (*s < '8'); ++s)
    n = (n << 3) | (*s - '0');
  return n;
}

void *
tar_find(char *tar, char const *name) {
  while(tar[0]) {
    if (strncmp(tar, name, 100) == 0)
      return tar + 0x200;
    size_t size = decode_oct(tar + 124);
    tar += 0x200 + (size + 0x1FF) & (SIZE_MAX ^ 0x1FF);
  }
  return NULL;
}
