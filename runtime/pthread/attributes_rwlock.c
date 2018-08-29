#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <klee/klee.h>

#include "attributes.h"

static pthread_attr_rwlock* get_rwlock_attr(const pthread_rwlockattr_t* a) {
  return *((pthread_attr_rwlock**) a);
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *a) {
  pthread_attr_rwlock* attr = (pthread_attr_rwlock*) malloc(sizeof(pthread_attr_rwlock));
  if (attr == NULL) {
    return ENOMEM;
  }

  memset(attr, 0, sizeof(pthread_attr_rwlock));

  attr->pshared = PTHREAD_PROCESS_PRIVATE;

  *((pthread_attr_rwlock**) a) = attr;
  return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a) {
  pthread_attr_rwlock* attr = get_rwlock_attr(a);
  free(attr);
  return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_rwlock_attr(a)->pshared;
  return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_rwlockattr_setpshared is not supported");

  get_rwlock_attr(a)->pshared = s;
  return 0;
}