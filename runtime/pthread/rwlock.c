#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define NO_WRITER_ACQUIRED (~((uint64_t)0))

static pthread_rwlock_t rwlockDefault = PTHREAD_RWLOCK_INITIALIZER;

static int __create_new_rwlock(__kpr_rwlock **rw) {
  __kpr_rwlock* lock = malloc(sizeof(__kpr_rwlock));
  if (lock == NULL) {
    return -1;
  }

  memset(lock, 0, sizeof(__kpr_rwlock));

  lock->acquiredWriter = NO_WRITER_ACQUIRED;

  lock->waitingReaderCount = 0;
  lock->waitingWriterCount = 0;

  __kpr_list_create(&lock->waitingList);
  __kpr_list_create(&lock->acquiredReaderLocks);

  *rw = lock;
  return 0;
}

static int __obtain_rwlock(pthread_rwlock_t *rwlock, __kpr_rwlock **dest) {
  // So first we have to check if we are any of the default static rwlock types
  if (__checkIfSame((char*) rwlock, (char*) &rwlockDefault)) {
    return __create_new_rwlock(dest);
  }

  *dest = *((__kpr_rwlock**) rwlock);

  return 0;
}

static __kpr_list_iterator __get_reader_lock(__kpr_rwlock *lock, uint64_t t) {
  __kpr_list_iterator it = __kpr_list_iterate(&lock->acquiredReaderLocks);

  while (__kpr_list_iterator_valid(it)) {
    uint64_t tid = (uint64_t) __kpr_list_iterator_value(it);

    if (tid == t) {
      return it;
    }

    __kpr_list_iterator_next(&it);
  }

  // At this point, this should be an invalid iterator
  return it;
}

static int rwlock_tryrdlock(__kpr_rwlock* lock) {
  uint64_t tid = klee_get_thread_id();

  if (lock->acquiredWriter != NO_WRITER_ACQUIRED) {
    if (lock->acquiredWriter == tid) {
      return EDEADLK;
    } else {
      return EBUSY;
    }
  }

  // So check if this is a multiple lock
  __kpr_list_iterator it = __get_reader_lock(lock, tid);
  if (__kpr_list_iterator_valid(it)) {
    __kpr_list_push(&lock->acquiredReaderLocks, (void*) tid);
    return 0;
  }

  // So if we have not locked this already than we have to check if any writers are waiting on the lock
  if (lock->waitingWriterCount > 0) {
    return EBUSY;
  } else {
    // No writers are locked so we can go ahead and try to lock this one as well
    __kpr_list_push(&lock->acquiredReaderLocks, (void*) tid);
    return 0;
  }
}

static int rwlock_trywrlock(__kpr_rwlock* lock) {
  uint64_t tid = klee_get_thread_id();

  if (lock->acquiredWriter != NO_WRITER_ACQUIRED) {
    if (lock->acquiredWriter == tid) {
      return EDEADLK;
    } else {
      return EBUSY;
    }
  }

  if (__kpr_list_size(&lock->acquiredReaderLocks) > 0) {
    return EBUSY;
  }

  lock->acquiredWriter = tid;
  return 0;
}

// Now the actual implementation

int pthread_rwlock_init(pthread_rwlock_t *l, const pthread_rwlockattr_t *attr) {
  klee_toggle_thread_scheduling(0);

  __kpr_rwlock* lock = malloc(sizeof(__kpr_rwlock));
  if (lock == NULL) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  memset(lock, 0, sizeof(__kpr_rwlock));

  *((__kpr_rwlock**)l) = lock;

  lock->acquiredWriter = NO_WRITER_ACQUIRED;

  lock->waitingReaderCount = 0;
  lock->waitingWriterCount = 0;

  __kpr_list_create(&lock->waitingList);
  __kpr_list_create(&lock->acquiredReaderLocks);

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);
  __kpr_rwlock* lock;

  if (__obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  if (__kpr_list_size(&lock->acquiredReaderLocks) != 0 || lock->acquiredWriter != NO_WRITER_ACQUIRED) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  free(lock);

  klee_toggle_thread_scheduling(1);
  return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);

  __kpr_rwlock* lock;
  if (__obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  int result = rwlock_tryrdlock(lock);
  while (result != 0) {
    uint64_t tid = klee_get_thread_id();
    __kpr_list_push(&lock->waitingList, (void*) tid);
    lock->waitingReaderCount++;

    klee_sleep_thread();

    // And try again if we did not succeed
    result = rwlock_tryrdlock(lock);
  }

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);

  __kpr_rwlock* lock;
  if (__obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  int result = rwlock_tryrdlock(lock);

  klee_toggle_thread_scheduling(1);

  return result;
}

//int pthread_rwlock_timedrdlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);

int pthread_rwlock_wrlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);

  __kpr_rwlock* lock;
  if (__obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  int result = rwlock_trywrlock(lock);
  while (result != 0) {
    uint64_t tid = klee_get_thread_id();
    __kpr_list_push(&lock->waitingList, (void*) tid);
    lock->waitingWriterCount++;

    klee_sleep_thread();

    // And try again if we did not succeed
    result = rwlock_trywrlock(lock);
  }

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);

  __kpr_rwlock* lock;
  if (__obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  int result = rwlock_trywrlock(lock);

  klee_toggle_thread_scheduling(1);

  return result;
}

//int pthread_rwlock_timedwrlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);

int pthread_rwlock_unlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);
  __kpr_rwlock* lock;

  if (__obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  bool unlockAll = false;
  bool validUnlock = false;

  // So first of all test if we are the writer
  if (lock->acquiredWriter == klee_get_thread_id()) {
    lock->acquiredWriter = NO_WRITER_ACQUIRED;
    unlockAll = true;
    validUnlock = true;
  } else if (__kpr_list_size(&lock->acquiredReaderLocks) > 0) {
    // Now we have to remove one lock from ourself, if we can actually remove it
    __kpr_list_iterator it = __get_reader_lock(lock, klee_get_thread_id());

    if (__kpr_list_iterator_valid(it)) {
      // Then remove the iterator and check the size
      __kpr_list_erase(&lock->acquiredReaderLocks, &it);

      validUnlock = true;

      // We can unlock all only if there are no locks left
      unlockAll = __kpr_list_size(&lock->acquiredReaderLocks) == 0;
    }
  }

  if (unlockAll) {
    __notify_threads(&lock->waitingList);
    lock->waitingReaderCount = 0;
    lock->waitingWriterCount = 0;
  }

  klee_toggle_thread_scheduling(1);
  return validUnlock ? 0 : -1;
}