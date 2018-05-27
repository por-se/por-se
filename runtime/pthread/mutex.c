#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static __pthread_impl_mutex* __obtain_mutex_data(pthread_mutex_t *mutex) {
  return *((__pthread_impl_mutex**)mutex);
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
  __pthread_impl_mutex* mutex = malloc(sizeof(__pthread_impl_mutex));
  memset(mutex, 0, sizeof(__pthread_impl_mutex));

  *((__pthread_impl_mutex**)m) = mutex;

  mutex->acquired = 0;
  mutex->holdingThread = 0;
  __stack_create(&mutex->waitingThreads);

  klee_warning("Lock init not acquired");
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0) {
    mutex->acquired = 1;
    mutex->holdingThread = tid;

    klee_warning("Got a lock - preempting");

    // We have acquired a lock, so make sure that we sync threads
    klee_preempt_thread();
    return 0;
  }

  klee_stack_trace();
  klee_warning("Lock already locked - sleeping");

  __stack_push(&mutex->waitingThreads, (void*) tid);
  klee_sleep_thread();
  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0 || mutex->holdingThread != tid) {
    return -1;
  }

  klee_warning("Lock unlocking - waking up threads");
  mutex->acquired = 0;

  __notify_threads(&mutex->waitingThreads);
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  if (mutex->acquired == 0) {
    return pthread_mutex_lock(m);
  }

  klee_preempt_thread();
  return EBUSY;
}

//int pthread_mutex_timedlock(pthread_mutex_t *__restrict, const struct timespec *__restrict);

int pthread_mutex_destroy(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  if (mutex->acquired == 1) {
    return EBUSY;
  }

  // Some real support for mutex destroy

  return 0;
}

//int pthread_mutex_consistent(pthread_mutex_t *);
//
//int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict, int *__restrict);
//int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict, int, int *__restrict);

//int pthread_mutexattr_destroy(pthread_mutexattr_t *);
//int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getpshared(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_gettype(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_init(pthread_mutexattr_t *);
//int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setrobust(pthread_mutexattr_t *, int);
//int pthread_mutexattr_settype(pthread_mutexattr_t *, int);
