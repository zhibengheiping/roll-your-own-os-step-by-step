#include <sys/io.h>
#include <linux/serial_reg.h>
#include <unistd.h>

static
void
serial_send_byte(unsigned short port, unsigned char data) {
  while ((inb(port + UART_LSR) & UART_LSR_THRE) == 0);
  outb(data + UART_TX, port);
}

ssize_t
write(int fd, void const *buf, size_t count) {
  if (fd != STDOUT_FILENO)
    return -1;
  unsigned char const *p = buf;
  for (size_t i=0; i<count; ++i)
    serial_send_byte(0x3f8, p[i]);
  return 0;
}
