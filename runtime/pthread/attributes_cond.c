#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <klee/klee.h>

#include "attributes.h"

static pthread_attr_cond* get_cond_attr(const pthread_condattr_t* a) {
  return *((pthread_attr_cond**) a);
}

int pthread_condattr_init(pthread_condattr_t *a) {
  pthread_attr_cond* attr = (pthread_attr_cond*) malloc(sizeof(pthread_attr_cond));
  if (attr == NULL) {
    return ENOMEM;
  }

  memset(attr, 0, sizeof(pthread_attr_cond));

  attr->clockid = 0;
  attr->pshared = PTHREAD_PROCESS_PRIVATE;

  *((pthread_attr_cond**) a) = attr;
  return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *a) {
  pthread_attr_cond* attr = get_cond_attr(a);
  free(attr);
  return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_cond_attr(a)->pshared;
  return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_condattr_setpshared is not supported");

  get_cond_attr(a)->pshared = s;
  return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t c) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_condattr_setclock is currently not supported\n");

  get_cond_attr(a)->clockid = c;
  return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *a, clockid_t *c) {
  if (a == NULL) {
    return -1;
  }

  *c = get_cond_attr(a)->clockid;
  return 0;
}
