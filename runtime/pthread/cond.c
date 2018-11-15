#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static pthread_cond_t condDefault = PTHREAD_COND_INITIALIZER;

static int kpr_create_new_cond(kpr_cond **c) {
  kpr_cond* cond = malloc(sizeof(kpr_cond));
  if (cond == 0) {
    return -1;
  }

  memset(cond, 0, sizeof(kpr_cond));

  kpr_list_create(&cond->waitingList);
  cond->waitingMutex = NULL;

  *c = cond;
  return 0;
}

static int kpr_obtain_cond(pthread_cond_t *cond, kpr_cond **dest) {
  // So first we have to check if we are any of the default static mutex types
  if (kpr_checkIfSame((char*) cond, (char*) &condDefault)) {
    return kpr_create_new_cond(dest);
  }

  *dest = *((kpr_cond**) cond);

  return 0;
}

int pthread_cond_init(pthread_cond_t *l, const pthread_condattr_t *attr) {
  klee_toggle_thread_scheduling(0);

  kpr_cond* lock = malloc(sizeof(kpr_cond));
  memset(lock, 0, sizeof(kpr_cond));

  *((kpr_cond**)l) = lock;

  lock->waitingMutex = NULL;
  kpr_list_create(&lock->waitingList);

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_cond_destroy(pthread_cond_t *l) {
  klee_toggle_thread_scheduling(0);

  kpr_cond* lock;

  if (kpr_obtain_cond(l, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  if (kpr_list_size(&lock->waitingList) != 0) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  free(lock);

  klee_toggle_thread_scheduling(1);
  return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  int result = kpr_mutex_unlock_internal(m);
  if (result != 0) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  kpr_cond* lock;
  if (kpr_obtain_cond(c, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  if (m != lock->waitingMutex) {
    if (lock->waitingMutex == NULL) {
      lock->waitingMutex = m;
    } else {
      klee_report_error(__FILE__, __LINE__, "Calling pthread_cond_wait with different mutexes results in undefined behaviour", "undef");
    }
  }

  uint64_t tid = klee_get_thread_id();
  kpr_list_push(&lock->waitingList, (void*) tid);

  klee_sleep_thread();

  klee_toggle_thread_scheduling(1);
  return pthread_mutex_lock(m);
}

// int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *kpr_restrict);

int pthread_cond_broadcast(pthread_cond_t *c) {
  klee_toggle_thread_scheduling(0);

  kpr_cond* lock;
  if (kpr_obtain_cond(c, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  kpr_notify_threads(&lock->waitingList);
  lock->waitingMutex = NULL;

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();
  return 0;
}

int pthread_cond_signal(pthread_cond_t *c) {
  klee_toggle_thread_scheduling(0);

  kpr_cond* lock;
  if (kpr_obtain_cond(c, &lock) != 0) {
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  if (kpr_list_size(&lock->waitingList) == 0) {
    klee_toggle_thread_scheduling(1);
    return 0;
  }

  uint64_t waiting = (uint64_t) kpr_list_pop(&lock->waitingList);
  klee_wake_up_thread(waiting);

  if (kpr_list_size(&lock->waitingList) == 0) {
    lock->waitingMutex = NULL;
  }

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return 0;
}
