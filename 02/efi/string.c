#include <string.h>

int
strcmp(char const *s1, char const *s2) {
  int c;
  unsigned char c1, c2;
  do {
    c1 = (unsigned char)*s1++;
    c2 = (unsigned char)*s2++;
    c = (c1 > c2) - (c1 < c2);
  } while ((c == 0) && c1);
  return c;
}

int
strncmp(char const *s1, char const *s2, size_t n) {
  for (size_t i=0; i<n; ++i) {
    unsigned char c1 = (unsigned char)s1[i];
    unsigned char c2 = (unsigned char)s2[i];
    int c = (c1 > c2) - (c1 < c2);
    if ((c != 0) || (c1 == 0))
      return c;
  }
  return 0;
}

void *
memcpy(void *dst, const void *src, size_t n) {
  char *d = dst;
  char const *s = src;
  for (size_t i=0; i<n; ++i)
    d[i] = s[i];
  return dst;
}
