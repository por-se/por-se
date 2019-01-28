#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define NO_WRITER_ACQUIRED (~((uint64_t)0))

static pthread_rwlock_t rwlockDefault = PTHREAD_RWLOCK_INITIALIZER;

static int kpr_create_new_rwlock(kpr_rwlock **rw) {
  kpr_rwlock* lock = malloc(sizeof(kpr_rwlock));
  if (lock == NULL) {
    return -1;
  }

  memset(lock, 0, sizeof(kpr_rwlock));

  lock->acquiredWriter = NO_WRITER_ACQUIRED;

  lock->waitingReaderCount = 0;
  lock->waitingWriterCount = 0;

  *rw = lock;
  return 0;
}

static int kpr_obtain_rwlock(pthread_rwlock_t *rwlock, kpr_rwlock **dest) {
  // So first we have to check if we are any of the default static rwlock types
  if (kpr_checkIfSame((char*) rwlock, (char*) &rwlockDefault)) {
    return kpr_create_new_rwlock(dest);
  }

  *dest = *((kpr_rwlock**) rwlock);

  return 0;
}

static int rwlock_tryrdlock(kpr_rwlock* lock) {
  uint64_t tid = klee_get_thread_id();

  if (lock->acquiredWriter != NO_WRITER_ACQUIRED) {
    if (lock->acquiredWriter == tid) {
      return EDEADLK;
    } else {
      return EBUSY;
    }
  }

  // So if we have not locked this already than we have to check if any writers are waiting on the lock
  if (lock->waitingWriterCount > 0) {
    return EBUSY;
  } else {
    // No writers are locked so we can go ahead and try to lock this one as well
    lock->acquiredReaderCount++;
    return 0;
  }
}

static int rwlock_trywrlock(kpr_rwlock* lock) {
  uint64_t tid = klee_get_thread_id();

  if (lock->acquiredWriter != NO_WRITER_ACQUIRED) {
    if (lock->acquiredWriter == tid) {
      return EDEADLK;
    } else {
      return EBUSY;
    }
  }

  if (lock->acquiredReaderCount > 0) {
    return EBUSY;
  }

  lock->acquiredWriter = tid;
  return 0;
}

// Now the actual implementation

int pthread_rwlock_init(pthread_rwlock_t *l, const pthread_rwlockattr_t *attr) {
  klee_toggle_thread_scheduling(0);

  kpr_rwlock* lock = malloc(sizeof(kpr_rwlock));
  if (lock == NULL) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  memset(lock, 0, sizeof(kpr_rwlock));

  *((kpr_rwlock**)l) = lock;

  lock->acquiredWriter = NO_WRITER_ACQUIRED;

  lock->waitingReaderCount = 0;
  lock->waitingWriterCount = 0;

  lock->acquiredReaderCount = 0;

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);
  kpr_rwlock* lock;

  if (kpr_obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  if (lock->acquiredReaderCount != 0 || lock->acquiredWriter != NO_WRITER_ACQUIRED) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  free(lock);

  klee_toggle_thread_scheduling(1);
  return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);

  kpr_rwlock* lock;
  if (kpr_obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  int result = rwlock_tryrdlock(lock);
  while (result != 0) {
    lock->waitingReaderCount++;

    klee_wait_on(l);

    // And try again if we did not succeed
    result = rwlock_tryrdlock(lock);
  }

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);

  kpr_rwlock* lock;
  if (kpr_obtain_rwlock(l, &lock) != 0) {
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

  kpr_rwlock* lock;
  if (kpr_obtain_rwlock(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  int result = rwlock_trywrlock(lock);
  while (result != 0) {
    lock->waitingWriterCount++;

    klee_wait_on(l);

    // And try again if we did not succeed
    result = rwlock_trywrlock(lock);
  }

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *l) {
  klee_toggle_thread_scheduling(0);

  kpr_rwlock* lock;
  if (kpr_obtain_rwlock(l, &lock) != 0) {
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
  kpr_rwlock* lock;

  if (kpr_obtain_rwlock(l, &lock) != 0) {
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
  } else if (lock->acquiredReaderCount > 0) {
    lock->acquiredReaderCount--;

    // We can unlock all only if there are no locks left
    unlockAll = lock->acquiredReaderCount == 0;
  }

  if (unlockAll) {
    klee_release_waiting(l, KLEE_RELEASE_ALL);

    lock->waitingReaderCount = 0;
    lock->waitingWriterCount = 0;
  }

  klee_toggle_thread_scheduling(1);
  return validUnlock ? 0 : -1;
}
