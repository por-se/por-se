// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t.bc 2>&1 | FileCheck %s

#include "klee/klee.h"

static klee_sync_primitive_t lock;

static uint8_t x = 0;

static void emit_events(void) {
  klee_lock_acquire(&lock);
  klee_lock_release(&lock);
}

static void thread(void* arg) {
  emit_events();

  switch (x) {
    case 0:
      klee_warning("switch: 0");
      break;
    case 1:
      klee_warning("switch: 1");
      break;
    case 2:
      klee_warning("switch: 2");
      break;
    default:
      klee_warning("switch: default");
  }
}

int main(void) {
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x >= 0);

  klee_create_thread(thread, NULL);

  emit_events();

  switch (x) {
    case 2:
      klee_warning("switch: 2");
      break;
    case 1:
      klee_warning("switch: 1");
      break;
    case 0:
      klee_warning("switch: 0");
      break;
    default:
      klee_warning("switch: default");
  }

  // CHECK: KLEE: done: completed paths = 8
  return 0;
}
