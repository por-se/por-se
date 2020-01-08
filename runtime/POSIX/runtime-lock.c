#include "runtime-lock.h"

#include <stdbool.h>
#include <assert.h>

static klee_sync_primitive_t runtime_lock;
static _Thread_local int runtime_lock_counter;

void kpr_acquire_runtime_lock() {
  if (runtime_lock_counter == 0) {
    klee_lock_acquire(&runtime_lock);
  }

  runtime_lock_counter++;
}

void kpr_release_runtime_lock() {
  assert(runtime_lock_counter > 0 && "Lock release with corresponding acquire missing");
  runtime_lock_counter--;

  if (runtime_lock_counter == 0) {
    klee_lock_release(&runtime_lock);
  }
}

klee_sync_primitive_t* kpr_runtime_lock() {
  return &runtime_lock;
}
