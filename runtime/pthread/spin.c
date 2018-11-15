#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

int pthread_spin_init(pthread_spinlock_t *t, int ignored) {
  return pthread_mutex_init((pthread_mutex_t*) t, NULL);
}

int pthread_spin_destroy(pthread_spinlock_t *t) {
  return pthread_mutex_destroy((pthread_mutex_t*) t);
}

int pthread_spin_lock(pthread_spinlock_t *t) {
  return pthread_mutex_lock((pthread_mutex_t*) t);
}

int pthread_spin_trylock(pthread_spinlock_t *t) {
  return pthread_mutex_trylock((pthread_mutex_t*) t);
}

int pthread_spin_unlock(pthread_spinlock_t *t) {
  return pthread_mutex_unlock((pthread_mutex_t*) t);
}
