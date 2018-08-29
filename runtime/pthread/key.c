#include "pthread_impl.h"

//int pthread_key_create(pthread_key_t *, void (*)(void *));
//int pthread_key_delete(pthread_key_t);
//void *pthread_getspecific(pthread_key_t);
//int pthread_setspecific(pthread_key_t, const void *);

#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static __pthread_impl_key* __obtain_pthread_key(pthread_key_t* b) {
  return *((__pthread_impl_key**)b);
}

int pthread_key_create(pthread_key_t *k, void (*destructor) (void*)) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_key* key = malloc(sizeof(__pthread_impl_key));
  if (key == NULL) {
    klee_toggle_thread_scheduling(1);
    return ENOMEM;
  }

  memset(key, 0, sizeof(__pthread_impl_key));

  key->destructor = destructor;
  key->value = NULL;

  *((__pthread_impl_key**)k) = key;

  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_key_delete(pthread_key_t k) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_key* key = __obtain_pthread_key(&k);
  free(key);

  klee_toggle_thread_scheduling(1);
  return 0;
}

void *pthread_getspecific(pthread_key_t k) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_key* key = __obtain_pthread_key(&k);
  void* value = key->value;

  klee_toggle_thread_scheduling(1);
  return value;
}

int pthread_setspecific(pthread_key_t k, const void *val) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_key* key = __obtain_pthread_key(&k);
  key->value = val;

  klee_toggle_thread_scheduling(1);
  return 0;
}