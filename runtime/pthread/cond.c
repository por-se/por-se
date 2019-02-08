#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "kpr/internal.h"

#include <stdlib.h>
#include <errno.h>

int pthread_cond_init(pthread_cond_t *lock, const pthread_condattr_t *attr) {
  lock->waitingMutex = NULL;
  lock->waitingCount = 0;

  return 0;
}

int pthread_cond_destroy(pthread_cond_t *lock) {
  if (lock->waitingCount != 0) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  return 0;
}

int pthread_cond_wait(pthread_cond_t *lock, pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  int result = kpr_mutex_unlock_internal(m);
  if (result != 0) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  if (m != lock->waitingMutex) {
    if (lock->waitingMutex == NULL) {
      lock->waitingMutex = m;
    } else {
      klee_report_error(__FILE__, __LINE__,
              "Calling pthread_cond_wait with different mutexes results in undefined behaviour",
              "undef");
    }
  }

  lock->waitingCount++;
  klee_wait_on(lock);

  return pthread_mutex_lock(m);
}

int pthread_cond_broadcast(pthread_cond_t *lock) {
  klee_toggle_thread_scheduling(0);

  // We can actually just use the transfer as we know that all wait on the
  // same mutex again
  klee_release_waiting(lock, KLEE_RELEASE_ALL);

  lock->waitingCount = 0;
  lock->waitingMutex = NULL;

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();
  return 0;
}

int pthread_cond_signal(pthread_cond_t *lock) {
  klee_toggle_thread_scheduling(0);

  // We can actually just use the transfer as we know that all wait on the
  // same mutex again
  klee_release_waiting(lock, KLEE_RELEASE_SINGLE);
  if (lock->waitingCount > 0) {
    lock->waitingCount--;
  }

  if (lock->waitingCount == 0) {
    lock->waitingMutex = NULL;
  }

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return 0;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *time) {
  klee_warning_once("pthread_cond_timedwait: timed lock not supported, calling pthread_cond_wait instead");
  return pthread_cond_wait(c, m);
}
