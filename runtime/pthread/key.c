#include "pthread_impl.h"

#include "klee/klee.h"
#include "key.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Note: the reason that we do not use the same technique as with the other mocked data structures is that on some
//       systems the pthread_key_t type will not be big enough to hold a 64 bit pointer value
//       this means we have to work with smaller data types

static unsigned int keyCount = 0;
static unsigned int keySpace = 0;
static kpr_key* keys = NULL;

static kpr_key* kpr_create_new_key(pthread_key_t *k) {
  if (keyCount >= keySpace) {
    // That means we have to enlarge it; default is to add 4
    keySpace += 4;
    kpr_key* newKeys = realloc(keys, sizeof(kpr_key) * keySpace);
    if (newKeys == NULL) {
      return NULL;
    }

    keys = newKeys;
  }

  *k = keyCount;
  keyCount++;

  return &keys[*k];
}

static kpr_key* kpr_obtain_pthread_key(pthread_key_t* b) {
  unsigned int* index = (unsigned int*) b;
  return &keys[*index];
}

int pthread_key_create(pthread_key_t *k, void (*destructor) (void*)) {
  klee_toggle_thread_scheduling(0);

  kpr_key* key = kpr_create_new_key(k);
  if (key == NULL) {
    klee_toggle_thread_scheduling(1);
    return ENOMEM;
  }

  memset(key, 0, sizeof(kpr_key));

  key->destructor = destructor;
  kpr_list_create(&key->values);

  klee_toggle_thread_scheduling(1);

  return 0;
}

static kpr_key_data* kpr_get_data(pthread_key_t k) {
  uint64_t tid = klee_get_thread_id();

  kpr_key* key = kpr_obtain_pthread_key(&k);

  kpr_list_iterator it = kpr_list_iterate(&key->values);
  while(kpr_list_iterator_valid(it)) {
    kpr_key_data* d = kpr_list_iterator_value(it);

    if (d->thread == tid) {
      return d;
    }

    kpr_list_iterator_next(&it);
  }

  kpr_key_data* d = (kpr_key_data*) malloc(sizeof(kpr_key_data));
  memset(d, 0, sizeof(kpr_key_data));
  d->thread = tid;
  d->value = NULL;

  kpr_list_push(&key->values, d);

  return d;
}

int pthread_key_delete(pthread_key_t k) {
  klee_toggle_thread_scheduling(0);

  kpr_key* key = kpr_obtain_pthread_key(&k);
  kpr_list_clear(&key->values);

  memset(key, 0, sizeof(kpr_key));

  klee_toggle_thread_scheduling(1);
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

// this is an internal method for the runtime to invoke all destructors that are associated with the
// keys that this thread created/used
void kpr_key_clear_data_of_thread(uint64_t tid) {
  unsigned i = 0;
  for (; i < keyCount; i++) {
    kpr_key* key = &keys[i];

    if (key->destructor == NULL) {
      continue;
    }

    // So we have a destructor that we may have to invoke
    kpr_list_iterator it = kpr_list_iterate(&key->values);
    for (; kpr_list_iterator_valid(it); kpr_list_iterator_next(&it)) {
      kpr_key_data* d = kpr_list_iterator_value(it);

      if (d->thread != tid) {
        continue;
      }

      // So there is a value associated and a destructor, make sure both is called
      key->destructor(d->value);
    }
  }
}
