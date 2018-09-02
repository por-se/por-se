#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static __kpr_barrier* __obtain_pthread_barrier(pthread_barrier_t* b) {
  return *((__kpr_barrier**)b);
}

int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *attr, unsigned count) {
  if (count == 0) {
    return EINVAL;
  }

  klee_toggle_thread_scheduling(0);

  __kpr_barrier* barrier = malloc(sizeof(__kpr_barrier));
  if (barrier == NULL) {
    klee_toggle_thread_scheduling(1);
    return ENOMEM;
  }

  memset(barrier, 0, sizeof(__kpr_barrier));

  barrier->count = count;
  barrier->currentCount = 0;
  __kpr_list_create(&barrier->waitingThreads);

  *((__kpr_barrier**)b) = barrier;

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) {
  klee_toggle_thread_scheduling(0);

  __kpr_barrier* barrier = __obtain_pthread_barrier(b);

  if (barrier->currentCount > 0) {
    klee_toggle_thread_scheduling(1);
    return EBUSY;
  }

  free(barrier);

  klee_toggle_thread_scheduling(1);
  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b) {
  klee_toggle_thread_scheduling(0);

  __kpr_barrier* barrier = __obtain_pthread_barrier(b);
  barrier->currentCount++;

  if (barrier->currentCount < barrier->count) {
    uint64_t tid = klee_get_thread_id();
    __kpr_list_push(&barrier->waitingThreads, (void*) tid);

    klee_toggle_thread_scheduling(1);
    klee_sleep_thread();

    return 0;
  } else if (barrier->currentCount == barrier->count) {
    barrier->currentCount = 0;
    __notify_threads(&barrier->waitingThreads);

    klee_toggle_thread_scheduling(1);
    klee_preempt_thread();

    return PTHREAD_BARRIER_SERIAL_THREAD;
  }

  klee_toggle_thread_scheduling(1);
  return EINVAL;
}