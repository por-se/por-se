#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "kpr/flags.h"
#include "kpr/internal.h"

#include <errno.h>
#include <assert.h>

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  kpr_ensure_valid(mutex);

  mutex->acquired = 0;
  mutex->holdingThread = NULL;
  mutex->robustState = KPR_MUTEX_NORMAL;

  if (attr != NULL) {
    pthread_mutexattr_gettype(attr, &mutex->type);
    pthread_mutexattr_getrobust(attr, &mutex->robust);
  }

  klee_por_register_event(por_lock_create, mutex);

  return 0;
}

static int kpr_mutex_trylock_internal(pthread_mutex_t* mutex) {
  pthread_t pthread = pthread_self();

  if (mutex->robustState == KPR_MUTEX_UNUSABLE) {
    return EINVAL;
  }

  if (mutex->acquired == 0) {
    mutex->acquired = 1;
    mutex->holdingThread = pthread;
    return 0;
  }

  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    if (mutex->holdingThread == pthread) {
      mutex->acquired++;

      // TODO: need to check overflows -> EAGAIN

      return 0;
    }
  }

  if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
    if (mutex->holdingThread == pthread) {
      return EDEADLK;
    }
  }

  return EBUSY;
}

int kpr_mutex_lock_internal(pthread_mutex_t *mutex, int* hasSlept) {
  int result;

  for (;;) {
    result = kpr_mutex_trylock_internal(mutex);

    if (result == 0 || result == EINVAL) {
      break;
    }

    // In the error check case, we have to prevent the deadlock
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && result == EDEADLK) {
      break;
    }

    if (mutex->robust == PTHREAD_MUTEX_ROBUST && result == EBUSY) {
      // Now we have to test if the owner is "dead"
      if (mutex->holdingThread != NULL && mutex->holdingThread->state != KPR_THREAD_STATE_LIVE) {
        mutex->robustState = KPR_MUTEX_INCONSISTENT;
        mutex->acquired = 1;
        mutex->holdingThread = pthread_self();

        result = EOWNERDEAD;
        break;
      }
    }

    if (hasSlept != NULL) {
      *hasSlept = 1;
    }

    klee_wait_on(mutex);
  }

  return result;
}

static inline void check_for_unsupported_acquire(int result) {
  // XXX: Since the current thread has now acquired the mutex, we would trigger
  // two lock_acquire events following each other. Our partial order does not currently
  // handle this case.
  if (result == EOWNERDEAD) {
    klee_report_error(__FILE__, __LINE__, "Reacquiring of robust mutex with owner being dead (unsupported)", "xxx.err");
  }
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  klee_toggle_thread_scheduling(0);
  kpr_check_if_valid(pthread_mutex_t, mutex);

  int hasSlept = 0;
  int result = kpr_mutex_lock_internal(mutex, &hasSlept);

  check_for_unsupported_acquire(result);

  if (mutex->acquired == 1) {
    klee_por_register_event(por_lock_acquire, mutex);
  }

  klee_toggle_thread_scheduling(1);
  if (hasSlept == 0) {
    klee_preempt_thread();
  }

  return result;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  klee_toggle_thread_scheduling(0);
  kpr_check_if_valid(pthread_mutex_t, mutex);

  int result = kpr_mutex_trylock_internal(mutex);

  check_for_unsupported_acquire(result);

  if (mutex->acquired == 1 && mutex->holdingThread == pthread_self()) {
    klee_por_register_event(por_lock_acquire, mutex);
  }

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return result;
}

int pthread_mutex_consistent(pthread_mutex_t *mutex) {
  int result = EINVAL;

  klee_toggle_thread_scheduling(0);
  kpr_check_if_valid(pthread_mutex_t, mutex);

  if (mutex->holdingThread == pthread_self() && mutex->robustState == KPR_MUTEX_INCONSISTENT) {
    mutex->robustState = KPR_MUTEX_NORMAL;
    result = 0;
  }
  klee_toggle_thread_scheduling(1);

  return result;
}

int kpr_mutex_unlock_internal(pthread_mutex_t *mutex) {
  pthread_t thread = pthread_self();

  if (mutex->acquired == 0 || mutex->holdingThread != thread) {
    if (mutex->type == PTHREAD_MUTEX_NORMAL && mutex->robust == PTHREAD_MUTEX_STALLED) {
      klee_report_error(__FILE__, __LINE__, "Unlocking a normal, nonrobust mutex results in undefined behavior", "undef");
    }

    // The return code for error checking mutexes or robust
    return EPERM;
  }

  if (mutex->robustState == KPR_MUTEX_INCONSISTENT) {
    mutex->robustState = KPR_MUTEX_UNUSABLE;
  }

  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    mutex->acquired--;

    if (mutex->acquired > 0) {
      return 0;
    }
  } else {
    mutex->acquired = 0;
  }

  klee_release_waiting(mutex, KLEE_RELEASE_ALL);

  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  klee_toggle_thread_scheduling(0);
  kpr_check_if_valid(pthread_mutex_t, mutex);

  int result = kpr_mutex_unlock_internal(mutex);
  if (result == 0 && mutex->acquired == 0) {
    klee_por_register_event(por_lock_release, mutex);
  }

  klee_toggle_thread_scheduling(1);

  klee_preempt_thread();

  return result;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  kpr_check_if_valid(pthread_mutex_t, mutex);

  if (mutex->acquired >= 1) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  klee_por_register_event(por_lock_destroy, mutex);

  return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *time) {
  klee_warning_once("pthread_mutex_timedlock: timed lock not supported, calling pthread_mutex_lock instead");
  return pthread_mutex_lock(mutex);
}

//int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict, int *__restrict);
//int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict, int, int *__restrict);
