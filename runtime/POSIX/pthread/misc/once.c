#include "klee/klee.h"
#include "klee/runtime/pthread.h"

int pthread_once(pthread_once_t *once, void (*oncefunc)(void)) {
  pthread_mutex_lock(&once->mutex);

  if (once->called != 0) {
    pthread_mutex_unlock(&once->mutex);
    return 0;
  }

  once->called = 1;
  pthread_mutex_unlock(&once->mutex);

  oncefunc();

  return 0;
}
