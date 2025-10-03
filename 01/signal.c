#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "signal.h"

__attribute__((constructor))
void
signal_setup_stack(void) {
  stack_t ss = {0};
  void *ss_addr = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
  ss.ss_sp = ss_addr;
  ss.ss_size = 8192;
  if (sigaltstack(&ss, NULL) == -1) {
    perror("sigaltstack");
    exit(EXIT_FAILURE);
  }
}

void
signal_set_handler(int signum, void (*handler)(int, siginfo_t *, void *)) {
  struct sigaction sa = { 0 };
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_ONSTACK|SA_SIGINFO;
  sa.sa_sigaction = handler;
  if (sigaction(signum, &sa, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}
