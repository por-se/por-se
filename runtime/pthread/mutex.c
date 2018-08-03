#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int __create_new_mutex(__pthread_impl_mutex **m) {
  __pthread_impl_mutex* mutex = malloc(sizeof(__pthread_impl_mutex));
  if (mutex == 0) {
    return -1;
  }

  memset(mutex, 0, sizeof(__pthread_impl_mutex));

  mutex->acquired = 0;
  mutex->holdingThread = 0;
  __stack_create(&mutex->waitingThreads);

  *m = mutex;
  return 0;
}

static __pthread_impl_mutex* __obtain_mutex_data(pthread_mutex_t *mutex) {
  int* value = (int*) mutex;
  if (*value == 0) {
    __pthread_impl_mutex* m = NULL;
    int ret = __create_new_mutex(&m);

    if (ret != 0) {
      return NULL;
    }

    return m;
  }

  return *((__pthread_impl_mutex**)mutex);
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_mutex *mutex = __obtain_mutex_data(m);
  if (mutex == NULL) {
    klee_toggle_thread_scheduling(0);
    return -1; // TODO check
  }

  if (attr == NULL) {
    int type = 0;
    pthread_mutexattr_gettype(attr, &type);
    mutex->type = type;
  }

  *((__pthread_impl_mutex**)m) = mutex;

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0) {
    mutex->acquired = 1;
    mutex->holdingThread = tid;

    // We have acquired a lock, so make sure that we sync threads
    klee_toggle_thread_scheduling(1);
    klee_preempt_thread();
    return 0;
  }

  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    if (mutex->holdingThread == pthread_self()) {
      mutex->acquired++;

      klee_toggle_thread_scheduling(1);
      klee_preempt_thread();
      return 0;
    }
  }

  __stack_push(&mutex->waitingThreads, (void*) tid);
  klee_toggle_thread_scheduling(1);
  klee_sleep_thread();

  return 0;
}

int __pthread_mutex_unlock_internal(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0 || mutex->holdingThread != tid) {
    return -1;
  }

  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    mutex->acquired--;
    if (mutex->acquired == 0) {
      __notify_threads(&mutex->waitingThreads);
    }
  } else {
    mutex->acquired = 0;
    __notify_threads(&mutex->waitingThreads);
  }

  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);
  int result = __pthread_mutex_unlock_internal(m);
  klee_toggle_thread_scheduling(1);

  if (result == 0) {
    klee_preempt_thread();
  }

  return result;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  if (mutex->acquired == 0) {
    klee_toggle_thread_scheduling(1);
    return pthread_mutex_lock(m);
  }

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return EBUSY;
}

//int pthread_mutex_timedlock(pthread_mutex_t *__restrict, const struct timespec *__restrict);

int pthread_mutex_destroy(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  if (mutex->acquired >= 1) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  free(mutex);

  // Some real support for mutex destroy
  klee_toggle_thread_scheduling(1);
  return 0;
}

//int pthread_mutex_consistent(pthread_mutex_t *);
//
//int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict, int *__restrict);
//int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict, int, int *__restrict);

static __pthread_impl_mutex_attr* __obtain_pthread_attr(const pthread_mutexattr_t* a) {
  return (__pthread_impl_mutex_attr*)a;
}

int pthread_mutexattr_init(pthread_mutexattr_t *a)  {
  __pthread_impl_mutex_attr* attr = (__pthread_impl_mutex_attr*) malloc(sizeof(__pthread_impl_mutex_attr));

  if (attr == NULL) {
    return ENOMEM;
  }

  memset(attr, 0, sizeof(__pthread_impl_mutex_attr));
  attr->type = PTHREAD_MUTEX_NORMAL;

  *((__pthread_impl_mutex_attr**)a) = attr;

  return 0;
}
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type) {
  __pthread_impl_mutex_attr* attr = __obtain_pthread_attr(a);
  attr->type = type;
  return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *type) {
  __pthread_impl_mutex_attr* attr = __obtain_pthread_attr(a);
  *type = attr->type;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a) {
  free(a);
  return 0;
}

//int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getpshared(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setrobust(pthread_mutexattr_t *, int);
