#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>

typedef void key_destructor(void*);

// Data structure used for the actual per thread storage
struct key_data {
  int generation;
  void* value;
};

// Global data associated with each key
struct key_metadata {
  bool used;
  int generation;

  key_destructor* destructor;
};

static _Thread_local struct key_data keyData[PTHREAD_KEYS_MAX];
static struct key_metadata keyMetaData[PTHREAD_KEYS_MAX];

// Helpers to correctly lock the meta data
static klee_sync_primitive_t keyMetadataLock;
static int _Thread_local metaLockCounter = 0;

// Basic recursive lock support without cond variables involved
static void lock_metadata() {
  if (metaLockCounter == 0) {
    klee_lock_acquire(&keyMetadataLock);
  }

  metaLockCounter++;
}

static void unlock_metadata() {
  assert(metaLockCounter > 0);
  metaLockCounter--;

  if (metaLockCounter == 0) {
    klee_lock_release(&keyMetadataLock);
  }
}

int pthread_key_create(pthread_key_t *k, void (*destructor) (void*)) {
  kpr_key* key = calloc(sizeof(kpr_key), 1);

  lock_metadata();
  
  // Find first unused slot
  int firstFree = -1;
  for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
    struct key_metadata* meta = &keyMetaData[i];

    if (!meta->used) {
      firstFree = i;

      meta->used = true;
      meta->destructor = destructor;
      meta->generation++;

      key->index = i;
      key->generation = meta->generation;
      break;
    }
  }

  unlock_metadata();

  if (firstFree == -1) {
    free(key);
    return EAGAIN;
  }

  *k = key;

  return 0;
}

int pthread_key_delete(pthread_key_t key) {
  lock_metadata();
  struct key_metadata* meta = &keyMetaData[key->index];

  if (meta->generation != key->generation || !meta->used) {
    // A no longer valid pthread_key_t is used to free this
    // key data -> invalid
    unlock_metadata();

    return EINVAL;
  }

  meta->used = false;
  meta->destructor = NULL;
  
  // Do not clear the meta->generation
  key->index = 0;

  unlock_metadata();

  free(key);
  return 0;
}

static struct key_data* kpr_get_data(pthread_key_t key) {
  // So we have to check that the current `key->generation`
  // matches the `data->generation` field

  struct key_data* data = &keyData[key->index];

  if (key->generation > data->generation) {
    data->generation = key->generation;
    data->value = NULL;
  }

  return data;
}

void *pthread_getspecific(pthread_key_t k) {
  struct key_data* data = kpr_get_data(k);
  void* val = data->value;
  return val;
}

int pthread_setspecific(pthread_key_t k, const void *val) {
  struct key_data* data = kpr_get_data(k);
  data->value = (void*)val;
  return 0;
}

// this is an internal method for the runtime to invoke all destructors that are associated with the
// keys that this thread created/used
void kpr_key_clear_data_of_thread(void) {
  bool cleanup_needed = false;

  for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
    struct key_data* data = &keyData[i];

    if (data->value != NULL) {
      cleanup_needed = false;
      break;
    }
  }

  if (!cleanup_needed) {
    // Avoid doing any actual cleanup if not needed
    // as we lock a global lock before
    return;
  }

  lock_metadata();

  for (int it = 0; it < PTHREAD_DESTRUCTOR_ITERATIONS; it++) {
    bool all_cleaned = true;

    for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
      struct key_data* data = &keyData[i];

      if (data->value != NULL) {
        struct key_metadata* meta = &keyMetaData[i];
        key_destructor* destr = meta->destructor;

        if (!meta->used) {
          // This can be a leftover if the key was deleted, but the
          // data was not cleared before. In this case, a destructor
          // should not be run
          data->value = NULL;
          continue;
        }

        all_cleaned = false;

        if (destr) {
          destr(data->value);
        } else {
          data->value = NULL;
        }
      }
    }

    if (all_cleaned) {
      break;
    }
  }

  unlock_metadata();
}
