#include "klee/klee.h"
#include "klee/runtime/pthread.h"

static void warnAboutMutex(void) {
  klee_warning_once("pthread_spinlock_t uses standard mutexes");
}

int pthread_spin_init(pthread_spinlock_t *t, int pshared) {
  warnAboutMutex();

  return pthread_mutex_init(t, NULL);
}

int pthread_spin_destroy(pthread_spinlock_t *t) {
  warnAboutMutex();

  return pthread_mutex_destroy(t);
}

int pthread_spin_lock(pthread_spinlock_t *t) {
  warnAboutMutex();

  return pthread_mutex_lock(t);
}

int pthread_spin_trylock(pthread_spinlock_t *t) {
  warnAboutMutex();

  return pthread_mutex_trylock(t);
}

int pthread_spin_unlock(pthread_spinlock_t *t) {
  warnAboutMutex();

  return pthread_mutex_unlock(t);
}
