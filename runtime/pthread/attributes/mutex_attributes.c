#include "klee/klee.h"
#include "klee/runtime/pthread.h"

/* Mutex attributes */

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
  attr->type = PTHREAD_MUTEX_DEFAULT;
  attr->type = PTHREAD_MUTEX_STALLED;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
  return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type) {
  *type = attr->type;
  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
  if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_ERRORCHECK && type != PTHREAD_MUTEX_RECURSIVE) {
    klee_report_error(__FILE__, __LINE__, "trying to set a mutex type that is unknown", "user");
  }

  attr->type = type;
  return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *attr, int *robust) {
  *robust = attr->robust;
  return 0;
}

int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int robust) {
  if (robust != PTHREAD_MUTEX_STALLED && robust != PTHREAD_MUTEX_ROBUST) {
    klee_report_error(__FILE__, __LINE__, "trying to set a mutex attr robust that is unknown", "user");
  }

  attr->robust = robust;
  return 0;
}