#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static __pthread_impl_barrier* __obtain_pthread_barrier(pthread_barrier_t* b) {
  return *((__pthread_impl_barrier**)b);
}

int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *attr, unsigned count) {
  if (count == 0) {
    return EINVAL;
  }

  klee_toggle_thread_scheduling(0);

  __pthread_impl_barrier* barrier = malloc(sizeof(__pthread_impl_barrier));
  if (barrier == NULL) {
    klee_toggle_thread_scheduling(1);
    return EAGAIN;
  }

  memset(barrier, 0, sizeof(__pthread_impl_barrier));

  barrier->count = count;
  barrier->currentCount = 0;
  __stack_create(&barrier->waitingThreads);

  printf("New barrier with start %u of %u\n", barrier->currentCount, barrier->count);

  *((__pthread_impl_barrier**)b) = barrier;

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_barrier* barrier = __obtain_pthread_barrier(b);

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

  __pthread_impl_barrier* barrier = __obtain_pthread_barrier(b);
  barrier->currentCount++;

  printf("Barrier has now the count of %u of %u\n", barrier->currentCount, barrier->count);

  if (barrier->currentCount < barrier->count) {
    uint64_t tid = klee_get_thread_id();
    __stack_push(&barrier->waitingThreads, (void*) tid);

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

//int pthread_barrierattr_destroy(pthread_barrierattr_t *);
//int pthread_barrierattr_getpshared(const pthread_barrierattr_t *__restrict, int *__restrict);
//int pthread_barrierattr_init(pthread_barrierattr_t *);
//int pthread_barrierattr_setpshared(pthread_barrierattr_t *, int);