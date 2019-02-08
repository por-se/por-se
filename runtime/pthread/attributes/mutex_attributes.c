#include "klee/klee.h"
#include "klee/runtime/pthread.h"

/* Mutex attributes */

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
  attr->type = PTHREAD_MUTEX_DEFAULT;
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