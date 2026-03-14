#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int
write_file_at(int dirfd, const char *filename, const char *id) {
  int fd = openat(dirfd, filename, O_WRONLY);
  if (fd < 0)
    return fd;
  int result = write(fd, id, strlen(id));
  close(fd);
  return result;
}

int
open_at(const char *dirname, const char *name, int flags) {
  int dirfd = open(dirname, O_PATH);
  if (dirfd < 0)
    return dirfd;

  int fd = openat(dirfd, name, flags);
  close(dirfd);
  return fd;
}
