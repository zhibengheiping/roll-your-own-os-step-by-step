#pragma once

#include <signal.h>

void signal_set_handler(int signum, void (*handler)(int, siginfo_t *, void *));
