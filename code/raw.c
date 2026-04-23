#include <assert.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <pty.h>
#include <sys/epoll.h>
#include <sys/wait.h>

#include <stdio.h>

static
int
epoll_add(int epfd, int fd, uint32_t events) {
  struct epoll_event e = {
    .events = events,
    .data = {.fd = fd},
  };
  return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e);
}


static
int
epoll_mod(int epfd, int fd, uint32_t events) {
  struct epoll_event e = {
    .events = events,
    .data = {.fd = fd},
  };
  return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &e);
}

int
main(int argc, char **argv) {
  assert(argc >= 2);

  int master, slave;
  assert(openpty(&master, &slave, NULL, NULL, NULL) == 0);

  pid_t pid = fork();
  assert(pid != -1);

  if (pid == 0) {
    close(master);
    assert(dup2(slave, STDIN_FILENO) != -1);
    assert(dup2(slave, STDOUT_FILENO) != -1);
    assert(dup2(slave, STDERR_FILENO) != -1);
    close(slave);
    setsid();
    execvp(argv[1], argv+1);
    assert(0);
  }

  close(slave);

  int fd = epoll_create1(EPOLL_CLOEXEC);
  assert(fd != -1);

  assert(epoll_add(fd, STDOUT_FILENO, 0) == 0);
  assert(epoll_add(fd, STDIN_FILENO, EPOLLIN) == 0);
  assert(epoll_add(fd, master, EPOLLIN) == 0);

  uint32_t master_mask = EPOLLIN;

  struct termios old;
  assert(tcgetattr(STDIN_FILENO, &old) == 0);
  struct termios raw;
  cfmakeraw(&raw);
  assert(tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0);

  char stdin_buf[4096];
  size_t stdin_start, stdin_end;
  char stdout_buf[4096];
  size_t stdout_start, stdout_end;

  uint32_t running = EPOLLIN | EPOLLOUT;

  while (running) {
    struct epoll_event e;
    assert(epoll_wait(fd, &e, 1, -1) == 1);

    if (e.data.fd == STDOUT_FILENO) {
      if (!(e.events & EPOLLOUT))
        continue;

      ssize_t n = write(STDOUT_FILENO, stdout_buf + stdout_start, stdout_end - stdout_start);
      if (n >= 0) {
        stdout_start += n;
        if (stdout_start < stdout_end)
          continue;

        assert(epoll_mod(fd, STDOUT_FILENO, 0) == 0);
        master_mask |= EPOLLIN;
        master_mask &= running;
        assert(epoll_mod(fd, master, master_mask) == 0);
      } else {
        assert((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR));
      }
    } else if (e.data.fd == STDIN_FILENO) {
      if (!(e.events & EPOLLIN))
        continue;

      ssize_t n = read(STDIN_FILENO, stdin_buf, sizeof(stdin_buf));
      assert(epoll_mod(fd, STDIN_FILENO, 0) == 0);
      if (n == 0)
        continue;

      if (n > 0) {
        stdin_start = 0;
        stdin_end = n;
        master_mask |= EPOLLOUT;
        master_mask &= running;
        assert(epoll_mod(fd, master, master_mask) == 0);
      } else {
        assert((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR));
      }
    } else if (e.data.fd == master) {
      if (e.events & EPOLLIN) {
        ssize_t n = read(master, stdout_buf, sizeof(stdout_buf));

        master_mask ^= EPOLLIN;
        if (n > 0) {
          stdout_start = 0;
          stdout_end = n;
          assert(epoll_mod(fd, STDOUT_FILENO, EPOLLOUT) == 0);
        } else if (n == 0) {
          running ^= EPOLLIN;
        } else {
          assert((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR));
        }
      }

      if (e.events & EPOLLOUT) {
        ssize_t n = write(master, stdin_buf + stdin_start, stdin_end - stdin_start);

        if (n >= 0) {
          stdin_start += n;
          if (stdin_start == stdin_end) {
            master_mask ^= EPOLLOUT;
            assert(epoll_mod(fd, STDIN_FILENO, EPOLLIN) == 0);
          }
        } else if (errno == EPIPE) {
          running ^= EPOLLOUT;
        } else {
          assert((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR));
        }
      }

      if (e.events & (EPOLLHUP | EPOLLERR)) {
        master_mask = 0;
        running = 0;
      }

      master_mask &= running;
      assert(epoll_mod(fd, master, master_mask) == 0);
    }
  }

  int status;
  assert(waitpid(pid, &status, 0) == pid);
  assert(tcsetattr(STDIN_FILENO, TCSANOW, &old) == 0);

  return WEXITSTATUS(status);
}
