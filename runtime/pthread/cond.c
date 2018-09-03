#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static __kpr_cond* __obtain_pthread_cond(pthread_cond_t *lock) {
  return *((__kpr_cond**) lock);
}

int pthread_cond_init(pthread_cond_t *l, const pthread_condattr_t *attr) {
  klee_toggle_thread_scheduling(0);

  __kpr_cond* lock = malloc(sizeof(__kpr_cond));
  memset(lock, 0, sizeof(__kpr_cond));

  *((__kpr_cond**)l) = lock;

  lock->mode = 0;
  __kpr_list_create(&lock->waitingList);
  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_cond_destroy(pthread_cond_t *l) {
  klee_toggle_thread_scheduling(0);

  __kpr_cond* lock = __obtain_pthread_cond(l);

  if (lock->mode != 0 || __kpr_list_size(&lock->waitingList) != 0) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  free(lock);

  klee_toggle_thread_scheduling(1);
  return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
  klee_toggle_thread_scheduling(0);

  int result = __pthread_mutex_unlock_internal(m);
  if (result != 0) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  __kpr_cond* lock = __obtain_pthread_cond(c);

  uint64_t tid = klee_get_thread_id();
  __kpr_list_push(&lock->waitingList, (void*) tid);

  klee_sleep_thread();

  klee_toggle_thread_scheduling(1);
  return pthread_mutex_lock(m);
}

// int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *__restrict);

int pthread_cond_broadcast(pthread_cond_t *c) {
  klee_toggle_thread_scheduling(0);
  __kpr_cond* lock = __obtain_pthread_cond(c);

  __notify_threads(&lock->waitingList);
  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();
  return 0;
}

int pthread_cond_signal(pthread_cond_t *c) {
  klee_toggle_thread_scheduling(0);
  __kpr_cond* lock = __obtain_pthread_cond(c);

  if (__kpr_list_size(&lock->waitingList) == 0) {
    klee_toggle_thread_scheduling(1);
    return 0;
  }

  uint64_t waiting = (uint64_t) __kpr_list_pop(&lock->waitingList);
  klee_wake_up_thread(waiting);

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return 0;
}