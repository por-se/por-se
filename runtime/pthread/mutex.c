#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

static pthread_mutex_t mutexDefault = PTHREAD_MUTEX_INITIALIZER;

static int kpr_create_new_mutex(kpr_mutex **m) {
  kpr_mutex* mutex = malloc(sizeof(kpr_mutex));
  if (mutex == 0) {
    return -1;
  }

  memset(mutex, 0, sizeof(kpr_mutex));

  mutex->acquired = 0;
  mutex->holdingThread = 0;
  kpr_list_create(&mutex->waitingThreads);

  *m = mutex;
  return 0;
}

static int kpr_obtain_mutex(pthread_mutex_t *mutex, kpr_mutex **dest) {
  // So first we have to check if we are any of the default static mutex types
  if (kpr_checkIfSame((char*) mutex, (char*) &mutexDefault)) {
    return kpr_create_new_mutex(dest);
  }

  *dest = *((kpr_mutex**) mutex);

  return 0;
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
  klee_toggle_thread_scheduling(0);

  kpr_mutex *mutex;
  int result = kpr_create_new_mutex(&mutex);

  if (result != 0) {
    klee_toggle_thread_scheduling(0);
    return -1; // TODO check
  }

  if (attr != NULL) {
    int type = 0;
    pthread_mutexattr_gettype(attr, &type);
    mutex->type = type;
  }

  *((kpr_mutex**)m) = mutex;

  klee_toggle_thread_scheduling(1);

  return 0;
}

static int kpr_mutex_trylock_internal(kpr_mutex* mutex) {
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

  if (false /* Error checking mutex */) {
    if (mutex->holdingThread == tid) {
      return EDEADLK;
    }
  }

  return EBUSY;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  kpr_mutex* mutex;
  if (kpr_obtain_mutex(m, &mutex) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  uint64_t tid = klee_get_thread_id();
  int sleptOnce = 0;

  while (kpr_mutex_trylock_internal(mutex) != 0) {
    kpr_list_push(&mutex->waitingThreads, (void*) tid);
    sleptOnce = 1;
    klee_sleep_thread();
  }

  klee_toggle_thread_scheduling(1);
  if (sleptOnce == 0) {
    klee_preempt_thread();
  }

  return 0;
}

int kpr_mutex_unlock_internal(pthread_mutex_t *m) {
  kpr_mutex* mutex;
  if (kpr_obtain_mutex(m, &mutex) != 0) {
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
      kpr_notify_threads(&mutex->waitingThreads);
    }
  } else {
    mutex->acquired = 0;
    kpr_notify_threads(&mutex->waitingThreads);
  }

  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);
  int result = kpr_mutex_unlock_internal(m);
  klee_toggle_thread_scheduling(1);

  if (result == 0) {
    klee_preempt_thread();
  }

  return result;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  kpr_mutex* mutex;
  if (kpr_obtain_mutex(m, &mutex) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  int result = kpr_mutex_trylock_internal(mutex);

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return result;
}

//int pthread_mutex_timedlock(pthread_mutex_t *__restrict, const struct timespec *__restrict);

int pthread_mutex_destroy(pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  kpr_mutex* mutex;
  if (kpr_obtain_mutex(m, &mutex) != 0) {
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
