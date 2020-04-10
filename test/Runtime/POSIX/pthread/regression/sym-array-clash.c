// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --standby-states=1 %t.bc

#include "klee/klee.h"

static klee_sync_primitive_t lock1;
static klee_sync_primitive_t lock2;

static void thread_a(void *arg) {
  uint8_t x;
  klee_lock_acquire(&lock1);
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x == 1);
  klee_lock_release(&lock1);

  klee_lock_acquire(&lock2);
  klee_assume(x != 0);
  klee_lock_release(&lock2);
}

static void thread_b(void *arg) {
  uint8_t x;
  klee_lock_acquire(&lock2);
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x == 0);
  klee_lock_release(&lock2);

  klee_lock_acquire(&lock1);
  klee_assume(x != 1);
  klee_lock_release(&lock1);
}

int main(void) {
  klee_create_thread(thread_a, NULL);
  klee_create_thread(thread_b, NULL);

  return 0;
}
