#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include <errno.h>

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void(*child)(void)) {
  klee_warning("pthread_atfork() is not supported as fork() is not handled by klee");
  return 0;
}

int pthread_cancel(pthread_t th) {
  klee_warning("pthread_cancel() is not supported");
  return EPERM;
}

int pthread_setcancelstate(int p1, int *p2) {
  klee_warning("pthread_setcancelstate() is not supported since pthread_cancel() is not supported");
  return EPERM;
}

int pthread_setcanceltype(int p1, int *p2) {
  klee_warning("pthread_setcanceltype() is not supported since pthread_cancel() is not supported");
  return EPERM;
}

void pthread_testcancel(void) {
  klee_warning("pthread_testcancel() is not supported since pthread_cancel() is not supported");
}