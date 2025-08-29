#include <stdio.h>
#include <unistd.h>
#include "task.h"

void
solong(void) {
  for (;;) {
    printf("So long\n");
    fflush(stdout);
    sleep(1);
    task_yield();
  }
}

void
thanks(void) {
  for (;;) {
    printf("Thanks for all the fish\n");
    fflush(stdout);
    sleep(1);
    task_yield();
  }
}

int
main(void) {
  struct task task1, task2;
  task_init(&task1, solong, 8192);
  task_init(&task2, thanks, 8192);
  task1.next = &task2;
  task2.next = &task1;
  task_main.next = &task1;
  task_yield();
  return 0;
}
