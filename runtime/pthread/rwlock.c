#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static __pthread_impl_rwlock* __obtain_pthread_rwlock(pthread_rwlock_t *lock) {
  return *((__pthread_impl_rwlock**) lock);
}

int pthread_rwlock_init(pthread_rwlock_t *l, const pthread_rwlockattr_t *attr) {
  __pthread_impl_rwlock* lock = malloc(sizeof(__pthread_impl_rwlock));
  memset(lock, 0, sizeof(__pthread_impl_rwlock));

  klee_mark_thread_shareable(lock);

  *((__pthread_impl_rwlock**)l) = lock;

  lock->acquiredWriter = 0;
  lock->mode = 0;
  lock->acquiredReaderCount = 0;

  __stack_create(&lock->waitingReaders);
  __stack_create(&lock->waitingWriters);

  return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *l) {
  __pthread_impl_rwlock* lock = __obtain_pthread_rwlock(l);
  return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *l) {
  int result = pthread_rwlock_tryrdlock(l);
  if (result != EBUSY) {
    return result;
  }

  __pthread_impl_rwlock* lock = __obtain_pthread_rwlock(l);

  // Otherwise the lock is currently locked by a writer
  uint64_t tid = klee_get_thread_id();
  __stack_push(&lock->waitingReaders, (void*) tid);
  klee_sleep_thread();

  lock->mode = 1; // Reader mode
  lock->acquiredReaderCount++;

  return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *l) {
  __pthread_impl_rwlock* lock = __obtain_pthread_rwlock(l);

  if (lock->mode == 0 || lock->mode == 1) {
    lock->mode = 1; // Reader mode
    lock->acquiredReaderCount++;
    return 0;
  }

  return EBUSY;
}

//int pthread_rwlock_timedrdlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);

int pthread_rwlock_wrlock(pthread_rwlock_t *l) {
  int result = pthread_rwlock_trywrlock(l);
  if (result != EBUSY) {
    return result;
  }

  __pthread_impl_rwlock* lock = __obtain_pthread_rwlock(l);

  // Otherwise the lock is currently locked by another writer or reader
  uint64_t tid = klee_get_thread_id();
  __stack_push(&lock->waitingWriters, (void*) tid);
  klee_sleep_thread();

  lock->mode = 2;
  lock->acquiredWriter = klee_get_thread_id();

  return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *l) {
  __pthread_impl_rwlock* lock = __obtain_pthread_rwlock(l);

  if (lock->mode != 0) {
    return EBUSY;
  }

  lock->mode = 2;
  lock->acquiredWriter = klee_get_thread_id();
  klee_preempt_thread();

  return 0;
}

//int pthread_rwlock_timedwrlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);

int pthread_rwlock_unlock(pthread_rwlock_t *l) {
  __pthread_impl_rwlock* lock = __obtain_pthread_rwlock(l);

  if (lock->mode == 0 || (lock->mode == 2 && lock->acquiredWriter != klee_get_thread_id())) {
    return EPERM;
  }
  // TODO: we currently do not check the readers

  if (lock->mode == 2) {
    lock->acquiredWriter = 0;
  } else if (lock->mode == 1) {
    lock->acquiredReaderCount--;

    if (lock->acquiredReaderCount > 0) {
      return 0;
    }
  }

  // For now always prefer writers
  if (__stack_size(&lock->waitingWriters) > 0) {
    // TODO: we should not just use the top one, but rather
    // make a symbolic value to determine the next writer
    uint64_t tid = (uint64_t) __stack_pop(&lock->waitingWriters);
    klee_wake_up_thread(tid);
    return 0;
  }

  if (__stack_size(&lock->waitingReaders) > 0) {
    __notify_threads(&lock->waitingReaders);
    klee_preempt_thread();
    return 0;
  }

  return 0;
}

//int pthread_rwlockattr_init(pthread_rwlockattr_t *);
//int pthread_rwlockattr_destroy(pthread_rwlockattr_t *);
//int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *, int);
//int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *__restrict, int *__restrict);