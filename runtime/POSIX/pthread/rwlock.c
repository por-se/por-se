#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "kpr/internal.h"

#include <errno.h>
#include <stdbool.h>

static int rwlock_tryrdlock(pthread_rwlock_t* lock) {
  pthread_t th = pthread_self();

  if (lock->acquiredWriter != NULL) {
    if (lock->acquiredWriter == th) {
      return EDEADLK;
    } else {
      return EBUSY;
    }
  }

  // No writers are locked so we can go ahead and try to lock this one as well
  lock->acquiredReaderCount++;
  return 0;
}

static int rwlock_trywrlock(pthread_rwlock_t* lock) {
  pthread_t th = pthread_self();

  if (lock->acquiredWriter != NULL) {
    if (lock->acquiredWriter == th) {
      return EDEADLK;
    } else {
      return EBUSY;
    }
  }

  if (lock->acquiredReaderCount > 0) {
    return EBUSY;
  }

  lock->acquiredWriter = th;
  return 0;
}

// Now the actual implementation

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr) {
  kpr_check_for_double_init(lock);
  kpr_ensure_valid(lock);

  lock->acquiredWriter = NULL;

  lock->acquiredReaderCount = 0;

  pthread_mutex_init(&lock->mutex, NULL);
  pthread_cond_init(&lock->cond, NULL);

  return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
  kpr_check_if_valid(pthread_rwlock_t, lock);

  if (lock->acquiredReaderCount != 0 || lock->acquiredWriter != NULL) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  pthread_mutex_destroy(&lock->mutex);
  pthread_cond_destroy(&lock->cond);

  return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock) {
  kpr_check_if_valid(pthread_rwlock_t, lock);

  pthread_mutex_lock(&lock->mutex);

  int result;
  for (;;) {
    result = rwlock_tryrdlock(lock);
    if (result != EBUSY) {
      break;
    }

    pthread_cond_wait(&lock->cond, &lock->mutex);

    // And try again if we did not succeed
  }

  pthread_mutex_unlock(&lock->mutex);

  return result;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock) {
  kpr_check_if_valid(pthread_rwlock_t, lock);

  pthread_mutex_lock(&lock->mutex);
  int result = rwlock_tryrdlock(lock);
  pthread_mutex_unlock(&lock->mutex);

  return result;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock) {
  kpr_check_if_valid(pthread_rwlock_t, lock);

  pthread_mutex_lock(&lock->mutex);

  int result;
  for (;;) {
    result = rwlock_trywrlock(lock);
    if (result != EBUSY) {
      break;
    }

    pthread_cond_wait(&lock->cond, &lock->mutex);

    // And try again if we did not succeed
  }

  pthread_mutex_unlock(&lock->mutex);

  return result;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *lock) {
  kpr_check_if_valid(pthread_rwlock_t, lock);

  pthread_mutex_lock(&lock->mutex);
  int result = rwlock_trywrlock(lock);
  pthread_mutex_unlock(&lock->mutex);

  return result;
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
  kpr_check_if_valid(pthread_rwlock_t, lock);

  pthread_mutex_lock(&lock->mutex);

  bool unlockAll = false;
  bool validUnlock = false;

  // So first of all test if we are the writer
  if (lock->acquiredWriter == pthread_self()) {
    lock->acquiredWriter = NULL;
    unlockAll = true;
    validUnlock = true;
  } else if (lock->acquiredReaderCount > 0) {
    lock->acquiredReaderCount--;
    validUnlock = true;

    // We can unlock all only if there are no locks left
    unlockAll = lock->acquiredReaderCount == 0;
  }

  if (unlockAll) {
    pthread_cond_broadcast(&lock->cond);
  }

  pthread_mutex_unlock(&lock->mutex);
  return validUnlock ? 0 : -1;
}


int pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *time) {
  klee_warning_once("pthread_rwlock_timedrdlock: timed lock not supported, calling pthread_rwlock_rdlock instead");
  return pthread_rwlock_rdlock(lock);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *time) {
  klee_warning_once("pthread_rwlock_timedwrlock: timed lock not supported, calling pthread_wrlock_rwlock instead");
  return pthread_rwlock_wrlock(lock);
}