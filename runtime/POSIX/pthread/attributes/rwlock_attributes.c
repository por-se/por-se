#include "klee/klee.h"
#include "klee/runtime/pthread.h"

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr) {
  attr->pshared = PTHREAD_PROCESS_PRIVATE;
  return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr) {
  return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr, int *pshared) {
  *pshared = attr->pshared;
  return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared) {
  if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED) {
    klee_report_error(__FILE__, __LINE__, "trying to set a pshared value that is unknown", "user");
  }

  attr->pshared = pshared;
  return 0;
}