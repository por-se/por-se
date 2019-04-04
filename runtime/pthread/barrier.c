#include <errno.h>

#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "kpr/internal.h"

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned count) {
  kpr_ensure_valid(barrier);

  if (count == 0) {
    return EINVAL;
  }

  barrier->count = count;
  barrier->currentCount = 0;

  pthread_mutex_init(&barrier->mutex, NULL);
  pthread_cond_init(&barrier->cond, NULL);

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
  kpr_check_if_valid(pthread_barrier_t, barrier);

  if (barrier->currentCount > 0) {
    return EBUSY;
  }

  barrier->count = 0;
  barrier->currentCount = 0;

  pthread_mutex_destroy(&barrier->mutex);
  pthread_cond_destroy(&barrier->cond);

  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
  if (barrier->count == 0) {
    klee_report_error(__FILE__, __LINE__, "Use of uninitialized/destroyed barrier", "user");
  }

  klee_toggle_thread_scheduling(0);
  kpr_check_if_valid(pthread_barrier_t, barrier);

  pthread_mutex_lock(&barrier->mutex);

  barrier->currentCount++;

  if (barrier->currentCount == barrier->count) {
    pthread_cond_broadcast(&barrier->cond);
    barrier->currentCount = 0;
  } else {
    pthread_cond_wait(&barrier->cond, &barrier->mutex);
  }

  pthread_mutex_unlock(&barrier->mutex);
  return 0;
}