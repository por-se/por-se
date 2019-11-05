#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "../kpr/key.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static kpr_list knownKeys = KPR_LIST_INITIALIZER;

int pthread_key_create(pthread_key_t *k, void (*destructor) (void*)) {
  kpr_key* key = calloc(sizeof(kpr_key), 1);

  key->destructor = destructor;
  kpr_list_create(&key->values);

  *k = key;

  klee_toggle_thread_scheduling(0);
  kpr_list_push(&knownKeys, key);
  klee_toggle_thread_scheduling(1);

  return 0;
}

static kpr_key_data* kpr_get_data(pthread_key_t k) {
  pthread_t th = pthread_self();

  kpr_key* key = (kpr_key*) k;

  kpr_list_iterator it = kpr_list_iterate(&key->values);
  while(kpr_list_iterator_valid(it)) {
    kpr_key_data* d = kpr_list_iterator_value(it);

    if (d->thread == th) {
      return d;
    }

    kpr_list_iterator_next(&it);
  }

  kpr_key_data* d = (kpr_key_data*) malloc(sizeof(kpr_key_data));
  memset(d, 0, sizeof(kpr_key_data));
  d->thread = th;
  d->value = NULL;

  kpr_list_push(&key->values, d);

  return d;
}

int pthread_key_delete(pthread_key_t k) {
  kpr_key* key = (kpr_key*) k;

  // Ensure that no destructor is called
  kpr_list_clear(&key->values);

  return 0;
}

void *pthread_getspecific(pthread_key_t k) {
  klee_toggle_thread_scheduling(0);

  kpr_key_data* data = kpr_get_data(k);
  void* val = data->value;

  klee_toggle_thread_scheduling(1);
  return val;
}

int pthread_setspecific(pthread_key_t k, const void *val) {
  klee_toggle_thread_scheduling(0);

  kpr_key_data* data = kpr_get_data(k);
  data->value = (void*)val;

  klee_toggle_thread_scheduling(1);
  return 0;
}

static void kpr_clear_thread_key(kpr_key* key, kpr_key_data* d) {
  if (key->destructor == NULL) {
    return;
  }

  // So we have to call the destructor for as long as the value is not NULL

  int runCount = 0;
  while (runCount < PTHREAD_DESTRUCTOR_ITERATIONS && d->value != NULL) {
    void *val = d->value;
    d->value = NULL;

    key->destructor(val);

    runCount++;
  }
}

static void kpr_clear_thread(kpr_key* key, pthread_t th) {
  kpr_list_iterator it = kpr_list_iterate(&key->values);
  while(kpr_list_iterator_valid(it)) {
    kpr_key_data* d = kpr_list_iterator_value(it);

    if (d->thread == th) {
      kpr_clear_thread_key(key, d);

      kpr_list_erase(&key->values, &it);
      return;
    }

    kpr_list_iterator_next(&it);
  }
}

// this is an internal method for the runtime to invoke all destructors that are associated with the
// keys that this thread created/used
void kpr_key_clear_data_of_thread(pthread_t th) {
  kpr_list_iterator it = kpr_list_iterate(&knownKeys);
  while(kpr_list_iterator_valid(it)) {
    kpr_key* k = kpr_list_iterator_value(it);

    kpr_clear_thread(k, th);

    kpr_list_iterator_next(&it);
  }
}
