// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t.bc

#include "klee/klee.h"

#include <assert.h>
#include <stdatomic.h>

static klee_sync_primitive_t lock;

static uint8_t x = 0;
atomic_int res = 0;

static void emit_events(void) {
  klee_lock_acquire(&lock);
  klee_lock_release(&lock);
}

static void thread(void* arg) {
  emit_events();

  if (x > 1) {
    klee_warning("larger than 1");
    ++res;
    assert(res < 2);
  }
}

int main(void) {
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x >= 0);

  klee_create_thread(thread, NULL);

  emit_events();

  if (x == 1) {
    klee_warning("equal to 1");
    ++res;
    assert(res < 2);
  }

  return 0;
}
