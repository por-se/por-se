#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <klee/klee.h>

#include "attributes.h"

static pthread_attr_thread* get_thread_attr(const pthread_attr_t* a) {
  return *((pthread_attr_thread**) a);
}

int pthread_attr_init(pthread_attr_t *a) {
  pthread_attr_thread* attr = (pthread_attr_thread*) malloc(sizeof(pthread_attr_t));
  if (attr == NULL) {
    return ENOMEM;
  }

  memset(attr, 0, sizeof(pthread_attr_thread));

  attr->scope = PTHREAD_SCOPE_PROCESS;
  attr->detachstate = PTHREAD_CREATE_JOINABLE;
  attr->stackaddr = NULL;
  attr->stacksize = 1024 * 1024;
  attr->guardsize = 1024 * 4; // Just assume it will be 4kb (which is a common page size)
  attr->inheritsched = PTHREAD_INHERIT_SCHED;
  attr->schedpolicy = SCHED_OTHER;

  *((pthread_attr_thread**) a) = attr;
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *a) {
  pthread_attr_thread* attr = get_thread_attr(a);
  free(attr);
  return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t *a, size_t *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_thread_attr(a)->guardsize;
  return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *a, size_t s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_attr_setguardsize is not supported");

  get_thread_attr(a)->guardsize = s;
  return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_thread_attr(a)->stacksize;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *a, size_t s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_attr_setstacksize is not supported");

  get_thread_attr(a)->stacksize = s;
  return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *a, int *ds) {
  if (a == NULL) {
    return -1;
  }

  *ds = get_thread_attr(a)->detachstate;
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *a, int ds) {
  if (a == NULL) {
    return -1;
  }

  if (ds != PTHREAD_CREATE_DETACHED && ds != PTHREAD_CREATE_JOINABLE) {
    return EINVAL;
  }

  get_thread_attr(a)->detachstate = ds;
  return 0;
}

int pthread_attr_getstack(const pthread_attr_t *a, void **v, size_t *s) {
  if (a == NULL) {
    return -1;
  }

  *v = get_thread_attr(a)->stackaddr;
  *s = get_thread_attr(a)->stacksize;
  return 0;
}

int pthread_attr_setstack(pthread_attr_t *a, void *v, size_t s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_attr_setstack is not supported");

  get_thread_attr(a)->stackaddr = v;
  get_thread_attr(a)->stacksize = s;
  return 0;
}

int pthread_attr_getscope(const pthread_attr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_thread_attr(a)->scope;
  return 0;
}

int pthread_attr_setscope(pthread_attr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_attr_setscope is not supported");

  get_thread_attr(a)->scope = s;
  return 0;
}

int pthread_attr_getschedpolicy(const pthread_attr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_thread_attr(a)->schedpolicy;
  return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_attr_setschedpolicy is not supported");

  get_thread_attr(a)->schedpolicy = s;
  return 0;
}

int pthread_attr_getschedparam(const pthread_attr_t *a, struct sched_param *sp) {
  return 0;
}

int pthread_attr_setschedparam(pthread_attr_t *a, const struct sched_param *sp) {
  klee_warning_once("pthread_attr_setschedparam is not supported");

  return 0;
}

int pthread_attr_getinheritsched(const pthread_attr_t *a, int *s) {
  if (a == NULL) {
    return -1;
  }

  *s = get_thread_attr(a)->inheritsched;
  return 0;
}

int pthread_attr_setinheritsched(pthread_attr_t *a, int s) {
  if (a == NULL) {
    return -1;
  }

  klee_warning_once("pthread_attr_setinheritsched is not supported");

  get_thread_attr(a)->inheritsched = s;
  return 0;
}