#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>
#include <errno.h>
#include <sys/epoll.h>

#include "virtio.h"


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

static
void
supply_buffer(struct virtio_queue *queue, void *buf, size_t len, int read) {
  struct iovec iovec = {
    .iov_base = buf,
    .iov_len = len,
  };

  int index = virtio_queue_alloc(queue, 1);
  assert(index >= 0);
  virtio_queue_writev(queue, index, &iovec, 1, read);
  virtio_queue_send(queue, index);
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

  struct vfio_pci_dev pci = {0};
  char const *devid = getenv("DEVID");
  if (devid == NULL)
    devid = "0000:00:03.0";

  vfio_pci_dev_open(devid, &pci);
  vfio_pci_dev_init(&pci);

  struct virtio_pci_dev dev = {0};
  virtio_pci_dev_init(&dev, &pci, 0);

  char *stdin_buf = vfio_pci_dev_map_dma(&pci, NULL, 4096, -1, 0);
  char *stdout_buf = vfio_pci_dev_map_dma(&pci, NULL, 4096, -1, 0);

  virtio_send_driver_ok(&dev);

  supply_buffer(&dev.queues[0], stdin_buf, 4096, 0);

  int fd = epoll_create1(EPOLL_CLOEXEC);
  assert(fd != -1);

  assert(epoll_add(fd, pci.epollfd, EPOLLIN) == 0);
  assert(epoll_add(fd, master, EPOLLIN) == 0);
  uint32_t master_mask = EPOLLIN;

  size_t stdin_start, stdin_end;
  size_t stdout_start, stdout_end;

  uint32_t running = EPOLLIN | EPOLLOUT;

  while (running) {
    struct epoll_event e;
    assert(epoll_wait(fd, &e, 1, -1) == 1);

    if (e.data.fd == master) {
      if (e.events & EPOLLIN) {
        ssize_t n = read(master, stdout_buf, sizeof(stdout_buf));

        master_mask ^= EPOLLIN;
        if (n > 0) {
          supply_buffer(&dev.queues[1], stdout_buf, n, 1);
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
            supply_buffer(&dev.queues[0], stdin_buf, 4096, 0);
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
    } else if (e.data.fd == pci.epollfd) {
      int64_t n = vfio_pci_dev_read_event(&pci, 0);
      if (n >= 0) {
        vfio_pci_dev_clear_irq(&pci, n);
        virtio_queue_handle_event(&dev.queues[n]);
        switch (n) {
        case 0: { // receiveq
          struct vring_used_elem *elem = virtio_queue_recv(&dev.queues[n]);
          if (elem != NULL) {
            stdin_start = 0;
            stdin_end = elem->len;
            master_mask |= EPOLLOUT;
            master_mask &= running;
            assert(epoll_mod(fd, master, master_mask) == 0);
            virtio_queue_free(&dev.queues[n], elem->id);
          }
          break;
        }
        case 1: { // transmitq
          struct vring_used_elem *elem = virtio_queue_recv(&dev.queues[n]);
          if (elem != NULL) {
            master_mask |= EPOLLIN;
            master_mask &= running;
            assert(epoll_mod(fd, master, master_mask) == 0);
            virtio_queue_free(&dev.queues[n], elem->id);
          }
          break;
        }
        }
      }
    }
  }

  return 0;
}
