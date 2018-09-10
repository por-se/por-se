#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <klee/klee.h>

#ifdef __APPLE__
#define PTHREAD_MUTEX_STALLED 1
#define PTHREAD_MUTEX_ROBUST 2

// int pthread_mutexattr_getrobust(const pthread_mutexattr_t *a, int *s);
// int pthread_mutexattr_setrobust(pthread_mutexattr_t *a, int s);
#endif

#include "attributes.h"

static pthread_attr_mutex* get_mutex_attr(const pthread_mutexattr_t* a) {
  return *((pthread_attr_mutex**) a);
}

int pthread_mutexattr_init(pthread_mutexattr_t *a) {
  pthread_attr_mutex* attr = (pthread_attr_mutex*) malloc(sizeof(pthread_attr_mutex));
  if (attr == NULL) {
    return ENOMEM;
  }

  memset(attr, 0, sizeof(pthread_attr_mutex));

  attr->robust = PTHREAD_MUTEX_STALLED;
  attr->type = PTHREAD_MUTEX_DEFAULT;
  attr->prioceiling = SCHED_FIFO;
  attr->protocol_ = PTHREAD_PRIO_NONE;
  attr->pshared = PTHREAD_PROCESS_PRIVATE;

  *((pthread_attr_mutex**) a) = attr;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a) {
  pthread_attr_mutex* attr = get_mutex_attr(a);
  free(attr);
  return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_mutex_attr(a)->type;
  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  get_mutex_attr(a)->type = s;
  return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_mutex_attr(a)->robust;
  return 0;
}

int pthread_mutexattr_setrobust(pthread_mutexattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  get_mutex_attr(a)->robust = s;
  return 0;
}

int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_mutex_attr(a)->prioceiling;
  return 0;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_mutexattr_setprioceiling is not supported");

  get_mutex_attr(a)->prioceiling = s;
  return 0;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_mutex_attr(a)->protocol_;
  return 0;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_mutexattr_setprotocol is not supported");

  get_mutex_attr(a)->protocol_ = s;
  return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_mutex_attr(a)->pshared;
  return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_mutexattr_setpshared is not supported");

  get_mutex_attr(a)->pshared = s;
  return 0;
}