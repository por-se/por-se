#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static __pthread_impl_barrier* __obtain_pthread_barrier(pthread_barrier_t* b) {
  return ((__pthread_impl_barrier*)b);
}

int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *attr, unsigned count) {
  if (count == 0) {
    return EINVAL;
  }

  __pthread_impl_barrier* barrier = malloc(sizeof(__pthread_impl_barrier));
  memset(barrier, 0, sizeof(__pthread_impl_barrier));

  *((__pthread_impl_barrier**)b) = barrier;

  barrier->count = count;
  barrier->currentCount = 0;
  __stack_create(&barrier->waitingThreads);

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) {
  __pthread_impl_barrier* barrier = __obtain_pthread_barrier(b);

  if (barrier->currentCount > 0) {
    return EBUSY;
  }

  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b) {
  __pthread_impl_barrier* barrier = __obtain_pthread_barrier(b);
  barrier->currentCount++;

  if (barrier->currentCount < barrier->count) {
    uint64_t tid = klee_get_thread_id();
    __stack_push(&barrier->waitingThreads, (void*) tid);
    klee_sleep_thread();
    return 0;
  } else if (barrier->currentCount == barrier->count) {
    barrier->currentCount = 0;
    __notify_threads(&barrier->waitingThreads);
    klee_preempt_thread();
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }

  return EINVAL;
}

//int pthread_barrierattr_destroy(pthread_barrierattr_t *);
//int pthread_barrierattr_getpshared(const pthread_barrierattr_t *__restrict, int *__restrict);
//int pthread_barrierattr_init(pthread_barrierattr_t *);
//int pthread_barrierattr_setpshared(pthread_barrierattr_t *, int);