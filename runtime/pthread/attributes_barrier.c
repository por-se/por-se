#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <klee/klee.h>

#ifdef __APPLE__
typedef struct {} pthread_barrierattr_t;

int pthread_barrierattr_destroy(pthread_barrierattr_t * a);
int pthread_barrierattr_getpshared(const pthread_barrierattr_t *a, int *p);
int pthread_barrierattr_init(pthread_barrierattr_t *a);
int pthread_barrierattr_setpshared(pthread_barrierattr_t *a, int p);
#endif

#include "attributes.h"

static pthread_attr_barrier* get_barrier_attr(const pthread_barrierattr_t* a) {
  return *((pthread_attr_barrier**) a);
}

int pthread_barrierattr_init(pthread_barrierattr_t *a) {
  pthread_attr_barrier* attr = (pthread_attr_barrier*) malloc(sizeof(pthread_attr_barrier));
  if (attr == NULL) {
    return ENOMEM;
  }

  memset(attr, 0, sizeof(pthread_attr_barrier));

  attr->pshared = PTHREAD_PROCESS_PRIVATE;

  *((pthread_attr_barrier**) a) = attr;
  return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *a) {
  pthread_attr_barrier* attr = get_barrier_attr(a);
  free(attr);
  return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_barrier_attr(a)->pshared;
  return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_barrierattr_setpshared is not supported");

  get_barrier_attr(a)->pshared = s;
  return 0;
}
