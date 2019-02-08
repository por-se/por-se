#include "klee/klee.h"
#include "klee/runtime/pthread.h"

int pthread_once(pthread_once_t *once, void (*oncefunc)(void)) {
  klee_toggle_thread_scheduling(0);

  if (*once != 0) {
    klee_toggle_thread_scheduling(1);
    return 0;
  }

  *once = 1;
  klee_toggle_thread_scheduling(1);

  oncefunc();

  return 0;
}