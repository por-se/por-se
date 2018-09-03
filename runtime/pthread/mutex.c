#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

static pthread_mutex_t mutexDefault = PTHREAD_MUTEX_INITIALIZER;

static int __create_new_mutex(__kpr_mutex **m) {
  __kpr_mutex* mutex = malloc(sizeof(__kpr_mutex));
  if (mutex == 0) {
    return -1;
  }

  memset(mutex, 0, sizeof(__kpr_mutex));

  mutex->acquired = 0;
  mutex->holdingThread = 0;
  __kpr_list_create(&mutex->waitingThreads);

  *m = mutex;
  return 0;
}

static int __obtain_mutex(pthread_mutex_t *mutex, __kpr_mutex **dest) {
  // So we want to check if we actually have a mutex that is valid
  if (!__checkIfSameSize(mutex, &mutexDefault)) {
    return -1;
  }

  // So first we have to check if we are any of the default static mutex types
  if (__checkIfSame(mutex, &mutexDefault)) {
    return __create_new_mutex(dest);
  }

  *dest = *((__kpr_mutex**) mutex);

  return 0;
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
  klee_toggle_thread_scheduling(0);

  __kpr_mutex *mutex;
  int result = __create_new_mutex(&mutex);

  if (result != 0) {
    klee_toggle_thread_scheduling(0);
    return -1; // TODO check
  }

  if (attr != NULL) {
    int type = 0;
    pthread_mutexattr_gettype(attr, &type);
    mutex->type = type;
  }

  *((__kpr_mutex**)m) = mutex;

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  __kpr_mutex* mutex;
  if (__obtain_mutex(m, &mutex) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

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
    if (mutex->holdingThread == klee_get_thread_id()) {
      mutex->acquired++;

      klee_toggle_thread_scheduling(1);
      klee_preempt_thread();
      return 0;
    }
  }

  if (false /* Error checking mutex */) {
    if (mutex->holdingThread == tid) {
      return EDEADLK;
    }
  }

  do {
    __kpr_list_push(&mutex->waitingThreads, (void*) tid);
    // klee_toggle_thread_scheduling(1);
    klee_sleep_thread();
    // klee_toggle_thread_scheduling(0);
  } while (mutex->acquired != 0);

  mutex->acquired = 1;
  mutex->holdingThread = tid;

  return 0;
}

int __pthread_mutex_unlock_internal(pthread_mutex_t *m) {
  __kpr_mutex* mutex;
  if (__obtain_mutex(m, &mutex) != 0) {
    return -1;
  }

  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0 || mutex->holdingThread != tid) {
    // The return code for error checking mutexes, but we will simply use
    // it in any case
    return EPERM;
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

  __kpr_mutex* mutex;

  if (__obtain_mutex(m, &mutex) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

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

  __kpr_mutex* mutex;
  if (__obtain_mutex(m, &mutex) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

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