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
  // We disable the thread scheduling here to not let the data race detection error
  // before we actually can acquire the mutex.
  // If the object is invalid the state is terminated in any case.
  // But in the case of a correct object the call to `pthread_mutex_lock` will reenable thread scheduling
  klee_toggle_thread_scheduling(0);

  if (barrier->count == 0) {
    klee_report_error(__FILE__, __LINE__, "Use of uninitialized/destroyed barrier", "user");
  }

  kpr_check_if_valid(pthread_barrier_t, barrier);

  pthread_mutex_lock(&barrier->mutex);

  barrier->currentCount++;

  int ret = 0;

  if (barrier->currentCount == barrier->count) {
    pthread_cond_broadcast(&barrier->cond);
    barrier->currentCount = 0;

    // Should only be returned to one thread (unspecified which one)
    // TODO: Maybe choice of thread can be made symbolic in the future?
    ret = PTHREAD_BARRIER_SERIAL_THREAD;
  } else {
    pthread_cond_wait(&barrier->cond, &barrier->mutex);
  }

  pthread_mutex_unlock(&barrier->mutex);

  return ret;
}