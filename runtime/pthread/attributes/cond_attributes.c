#include "klee/klee.h"
#include "klee/runtime/pthread.h"

int pthread_condattr_init(pthread_condattr_t *attr) {
  attr->pshared = PTHREAD_PROCESS_PRIVATE;
  attr->clock = CLOCK_REALTIME;
  return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
  return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t *attr, int *pshared) {
  *pshared = attr->pshared;
  return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared) {
  if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED) {
    klee_report_error(__FILE__, __LINE__, "trying to set a pshared value that is unknown", "user");
  }

  attr->pshared = pshared;
  return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *clock) {
  *clock = attr->clock;
  return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) {
  attr->clock = clock;
  return 0;
}