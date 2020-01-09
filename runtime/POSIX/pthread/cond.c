#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "kpr/internal.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

int pthread_cond_init(pthread_cond_t *lock, const pthread_condattr_t *attr) {
  kpr_check_for_double_init(lock);
  kpr_ensure_valid(lock);

  lock->waitingMutex = NULL;
  lock->waitingCount = 0;

  // We cannot register this create event as this might introduce a scheduling
  // point that we do not want to have.
  // klee_por_register_event(por_lock_create, &lock->lock);
  klee_por_register_event(por_condition_variable_create, &lock->internalCond);

  kpr_ensure_valid(lock);

  return 0;
}

int pthread_cond_destroy(pthread_cond_t *lock) {
  kpr_check_if_valid(pthread_cond_t, lock);

  if (lock->waitingCount != 0) {
    return EBUSY;
  }

  memset(lock, 0xAB, sizeof(pthread_cond_t));

  klee_por_register_event(por_condition_variable_destroy, &lock->internalCond);
  // klee_por_register_event(por_lock_destroy, &lock->lock);

  return 0;
}

int pthread_cond_wait(pthread_cond_t *lock, pthread_mutex_t *m) {
  kpr_check_if_valid(pthread_mutex_t, m);
  kpr_check_if_valid(pthread_cond_t, lock);

  klee_lock_acquire(&lock->lock);

  int acquiredCount = m->acquired;
  int result = kpr_mutex_unlock(m, true /* force unlock */);
  if (result != 0) {
    klee_lock_release(&lock->lock);
    return EINVAL;
  }

  assert(acquiredCount >= 1);

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
  klee_cond_wait(&lock->internalCond, &lock->lock);

  klee_lock_release(&lock->lock);

  result = pthread_mutex_lock(m);

  // TODO: how to handle a robust mutex which owner is now dead (?)
  if (result == 0) {
    // Subtract one since we already did one `pthread_mutex_lock`
    acquiredCount--;

    // If the mutex was a recursive one, then the `acquiredCount` is still greater
    // than zero -> we want to return the mutex in the same state as it was before
    // entering pthread_cond_wait -> Now we have to do mutex_lock calls until we reach
    // the same acquiredCount as before
    for (; acquiredCount > 0; acquiredCount--) {
      pthread_mutex_lock(m);
    }
  }

  return result;
}

int pthread_cond_broadcast(pthread_cond_t *lock) {
  kpr_check_if_valid(pthread_cond_t, lock);

  klee_lock_acquire(&lock->lock);

  // if (lock->waitingMutex != NULL) {
  //   if (lock->waitingMutex->holdingThread != pthread_self()) {
  //     // TODO: warn about other thread that is currently holding the mutex
  //   }
  // } else {
  //   // TODO: warn about mutex not locked
  // }

  klee_cond_broadcast(&lock->internalCond);

  lock->waitingCount = 0;
  lock->waitingMutex = NULL;

  klee_lock_release(&lock->lock);

  return 0;
}

int pthread_cond_signal(pthread_cond_t *lock) {
  kpr_check_if_valid(pthread_cond_t, lock);

  klee_lock_acquire(&lock->lock);

  // if (lock->waitingMutex != NULL) {
  //   if (lock->waitingMutex->holdingThread != pthread_self()) {
  //     // TODO: warn about other thread that is currently holding the mutex
  //   }
  // } else {
  //   // TODO: warn about mutex not locked
  // }

  klee_cond_signal(&lock->internalCond);
  if (lock->waitingCount > 0) {
    lock->waitingCount--;
  }

  if (lock->waitingCount == 0) {
    lock->waitingMutex = NULL;
  }

  klee_lock_release(&lock->lock);

  return 0;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *time) {
  klee_warning_once("pthread_cond_timedwait: timed lock not supported, calling pthread_cond_wait instead");
  return pthread_cond_wait(c, m);
}
