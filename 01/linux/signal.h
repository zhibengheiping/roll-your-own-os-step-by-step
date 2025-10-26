#pragma once

#include <signal.h>

void signal_init(void);
void signal_set_handler(int signum, void (*handler)(int, siginfo_t *, void *));
