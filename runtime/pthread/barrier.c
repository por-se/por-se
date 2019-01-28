#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static kpr_barrier* kpr_obtain_pthread_barrier(pthread_barrier_t* b) {
  return *((kpr_barrier**)b);
}

int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *attr, unsigned count) {
  if (count == 0) {
    return EINVAL;
  }

  klee_toggle_thread_scheduling(0);

  kpr_barrier* barrier = malloc(sizeof(kpr_barrier));
  if (barrier == NULL) {
    klee_toggle_thread_scheduling(1);
    return ENOMEM;
  }

  memset(barrier, 0, sizeof(kpr_barrier));

  barrier->count = count;
  barrier->currentCount = 0;

  *((kpr_barrier**)b) = barrier;

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) {
  klee_toggle_thread_scheduling(0);

  kpr_barrier* barrier = kpr_obtain_pthread_barrier(b);

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

  kpr_barrier* barrier = kpr_obtain_pthread_barrier(b);
  barrier->currentCount++;

  if (barrier->currentCount < barrier->count) {
    klee_wait_on(b);

    klee_toggle_thread_scheduling(1);
    return 0;
  } else if (barrier->currentCount == barrier->count) {
    barrier->currentCount = 0;
    klee_release_waiting(b, KLEE_RELEASE_ALL);

    klee_toggle_thread_scheduling(1);
    klee_preempt_thread();

    return PTHREAD_BARRIER_SERIAL_THREAD;
  }

  klee_toggle_thread_scheduling(1);
  return EINVAL;
}
