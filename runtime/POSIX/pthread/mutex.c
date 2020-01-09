#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "kpr/flags.h"
#include "kpr/internal.h"

#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

static bool kpr_mutex_default(pthread_mutex_t* mutex) {
  int trylock_support = mutex->trylock_support;

  if (trylock_support == KPR_TRYLOCK_UNKNOWN) {
    // get the global default value (...)
    trylock_support = KPR_TRYLOCK_DISABLED;
  }

  return (
    mutex->type == PTHREAD_MUTEX_NORMAL &&
    mutex->robust == PTHREAD_MUTEX_STALLED &&
    trylock_support == KPR_TRYLOCK_DISABLED
  );
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  kpr_check_for_double_init(mutex);
  kpr_ensure_valid(mutex);

  mutex->acquired = 0;
  mutex->holdingThread = NULL;

  mutex->type = PTHREAD_MUTEX_DEFAULT;
  mutex->robust = PTHREAD_MUTEX_STALLED;

  mutex->robustState = KPR_MUTEX_NORMAL;
  mutex->trylock_support = KPR_TRYLOCK_UNKNOWN /* better: get global default */;

  if (attr != NULL) {
    pthread_mutexattr_gettype(attr, &mutex->type);
    pthread_mutexattr_getrobust(attr, &mutex->robust);
    kpr_pthread_mutexattr_gettrylock(attr, &mutex->trylock_support);
  }

  klee_por_register_event(por_lock_create, &mutex->lock);
  if (!kpr_mutex_default(mutex)) {
    klee_por_register_event(por_condition_variable_create, &mutex->cond);
  }

  kpr_ensure_valid(mutex);

  return 0;
}

static int pthread_mutex_lock_internal(pthread_mutex_t *mutex, bool may_block) {
  if (mutex->robust == PTHREAD_MUTEX_ROBUST && mutex->robustState == KPR_MUTEX_UNUSABLE) {
    return EINVAL;
  }

  if (mutex->acquired == 0) {
    // Not yet acquired by anyone
    mutex->acquired = 1;
    mutex->holdingThread = pthread_self();
    return 0;
  }

  assert(mutex->holdingThread != NULL);

  // So the lock is currently acquired by someone else
  if (mutex->holdingThread == pthread_self()) {
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
      return EDEADLOCK;
    }

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
      // type is recursive
      mutex->acquired++;
      return 0;
    }

    assert(mutex->type == PTHREAD_MUTEX_NORMAL);

    // Report a double locking -> short circuit by locking `&mutex->lock` again
    // ugly but works
    klee_lock_acquire(&mutex->lock);
    assert(0);
    return -1;
  }

  while(1) {
    // The mutex is currently acquired and it is not acquired by ourself

    if (mutex->robust == PTHREAD_MUTEX_ROBUST) {
      if (mutex->robustState == KPR_MUTEX_UNUSABLE) {
        return EINVAL;
      }

      // We have to test if the owner is dead -> then we can get the mutex
      assert(mutex->holdingThread != NULL);

      if (mutex->holdingThread->state != KPR_THREAD_STATE_LIVE) {
        mutex->robustState = KPR_MUTEX_INCONSISTENT;
        mutex->acquired = 1;
        mutex->holdingThread = pthread_self();
        return EOWNERDEAD;
      }
    }

    if (!may_block) {
      return EBUSY;
    }

    // Wait until someone will release the mutex
    klee_cond_wait(&mutex->cond, &mutex->lock);

    if (mutex->acquired == 0) {
      mutex->acquired = 1;
      mutex->holdingThread = pthread_self();
      return 0;
    }
  }
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  kpr_check_if_valid(pthread_mutex_t, mutex);

  klee_lock_acquire(&mutex->lock);

  if (kpr_mutex_default(mutex)) {
    mutex->acquired = 1;
    mutex->holdingThread = pthread_self();
    return 0;
  }

  int ret = pthread_mutex_lock_internal(mutex, true);
  klee_lock_release(&mutex->lock);
  return ret;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  kpr_check_if_valid(pthread_mutex_t, mutex);

  if (kpr_mutex_default(mutex)) {
    // Currently unsupported
    klee_report_error(__FILE__, __LINE__, "trying to use trylock on a basic mutex - unsupported", "user");
    return -1;
  }

  klee_lock_acquire(&mutex->lock);

  int ret = pthread_mutex_lock_internal(mutex, false);
  klee_lock_release(&mutex->lock);
  return ret;
}

int pthread_mutex_consistent(pthread_mutex_t *mutex) {
  kpr_check_if_valid(pthread_mutex_t, mutex);

  klee_lock_acquire(&mutex->lock);

  if (mutex->robust != PTHREAD_MUTEX_ROBUST) {
    klee_lock_release(&mutex->lock);
    return EINVAL;
  }

  int result = 0;

  if (mutex->holdingThread != pthread_self() || mutex->robustState != KPR_MUTEX_INCONSISTENT) {
    result = EINVAL;
  } else {
    mutex->robustState = KPR_MUTEX_NORMAL;
  }

  klee_lock_release(&mutex->lock);

  return result;
}

int kpr_mutex_unlock(pthread_mutex_t *mutex, bool force) {
  if (kpr_mutex_default(mutex)) {
    mutex->acquired = 0;
    mutex->holdingThread = NULL;

    klee_lock_release(&mutex->lock);
    return 0;
  }

  klee_lock_acquire(&mutex->lock);
  if (mutex->acquired == 0) {
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
      klee_lock_release(&mutex->lock);
      return EPERM;
    }

    klee_report_error(__FILE__, __LINE__, "trying to unlock a mutex that is not locked", "user");
    klee_lock_release(&mutex->lock);
    return -1;
  }

  if (mutex->holdingThread != pthread_self()) {
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
      klee_lock_release(&mutex->lock);
      return EPERM;
    }

    klee_report_error(__FILE__, __LINE__, "trying to unlock a mutex that is locked by another thread", "user");
    klee_lock_release(&mutex->lock);
    return -1;
  }

  bool unlock;
  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    assert(mutex->acquired > 0);
    mutex->acquired--;

    unlock = mutex->acquired == 0 || force;
  } else {
    unlock = true;
  }

  if (mutex->robust == PTHREAD_MUTEX_ROBUST && unlock) {
    // We have to check whether the mutex is was from a dead thread and is
    // now made consistent again.
    // If this is not the case and we actually have to unlock the mutex,
    // then the mutex will become unusable

    if (mutex->robustState == KPR_MUTEX_INCONSISTENT) {
      mutex->robustState = KPR_MUTEX_UNUSABLE;
    }
  }

  if (unlock) {
    mutex->acquired = 0;
    mutex->holdingThread = NULL;
    klee_cond_signal(&mutex->cond);
  }

  klee_lock_release(&mutex->lock);

  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  kpr_check_if_valid(pthread_mutex_t, mutex);

  return kpr_mutex_unlock(mutex, false);
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  kpr_check_if_valid(pthread_mutex_t, mutex);

  if (mutex->acquired >= 1) {
    return EBUSY;
  }

  // 0xAB is the random pattern of klee
  memset(mutex, 0xAB, sizeof(pthread_mutex_t));

  if (!kpr_mutex_default(mutex)) {
    klee_por_register_event(por_condition_variable_destroy, &mutex->cond);
  }
  klee_por_register_event(por_lock_destroy, &mutex->lock);

  return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *time) {
  klee_warning_once("pthread_mutex_timedlock: timed lock not supported, calling pthread_mutex_trylock instead");
  return pthread_mutex_trylock(mutex);
}

//int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict, int *__restrict);
//int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict, int, int *__restrict);
