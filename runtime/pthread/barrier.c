#include <errno.h>

#include "klee/klee.h"
#include "klee/runtime/pthread.h"

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned count) {
  if (count == 0) {
    return EINVAL;
  }

  barrier->count = count;
  barrier->currentCount = 0;

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
  if (barrier->currentCount > 0) {
    return EBUSY;
  }

  barrier->count = 0;
  barrier->currentCount = 0;

  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
  if (barrier->count == 0) {
    klee_report_error(__FILE__, __LINE__, "Use of uninitialized/destroyed barrier", "user");
  }

  klee_toggle_thread_scheduling(0);

  barrier->currentCount++;

  if (barrier->currentCount < barrier->count) {
    klee_wait_on(barrier);

    klee_toggle_thread_scheduling(1);
    return 0;
  }

  if (barrier->currentCount == barrier->count) {
    barrier->currentCount = 0;
    klee_release_waiting(barrier, KLEE_RELEASE_ALL);

    klee_toggle_thread_scheduling(1);
    klee_preempt_thread();

    return PTHREAD_BARRIER_SERIAL_THREAD;
  }

  klee_toggle_thread_scheduling(1);
  return EINVAL;
}