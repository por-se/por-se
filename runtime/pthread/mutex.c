#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include <errno.h>
#include <assert.h>

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  mutex->acquired = 0;
  mutex->holdingThread = 0;

  if (attr != NULL) {
    int type = 0;
    pthread_mutexattr_gettype(attr, &type);
    mutex->type = type;
  }

  return 0;
}

static int kpr_mutex_trylock_internal(pthread_mutex_t* mutex) {
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0) {
    mutex->acquired = 1;
    mutex->holdingThread = tid;
    return 0;
  }

  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    if (mutex->holdingThread == tid) {
      mutex->acquired++;
      return 0;
    }
  }

//  if (false /* Error checking mutex */) {
//    if (mutex->holdingThread == tid) {
//      return EDEADLK;
//    }
//  }

  return EBUSY;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  klee_toggle_thread_scheduling(0);

  int sleptOnce = 0;

  while (kpr_mutex_trylock_internal(mutex) != 0) {
    sleptOnce = 1;
    klee_wait_on(mutex);
  }

  klee_toggle_thread_scheduling(1);
  if (sleptOnce == 0) {
    klee_preempt_thread();
  }

  return 0;
}

int kpr_mutex_unlock_internal(pthread_mutex_t *mutex) {
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0 || mutex->holdingThread != tid) {
    // The return code for error checking mutexes, but we will simply use
    // it in any case
    return EPERM;
  }

  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    mutex->acquired--;

    if (mutex->acquired > 0) {
      return 0;
    }
  } else {
    mutex->acquired = 0;
  }

  klee_release_waiting(mutex, KLEE_RELEASE_SINGLE);

  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);
  int result = kpr_mutex_unlock_internal(m);
  klee_toggle_thread_scheduling(1);

  klee_preempt_thread();

  return result;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  klee_toggle_thread_scheduling(0);

  int result = kpr_mutex_trylock_internal(mutex);

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return result;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  if (mutex->acquired >= 1) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *time) {
  klee_warning_once("pthread_mutex_timedlock: timed lock not supported, calling pthread_mutex_lock instead");
  return pthread_mutex_lock(mutex);
}

//int pthread_mutex_consistent(pthread_mutex_t *);
//
//int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict, int *__restrict);
//int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict, int, int *__restrict);
