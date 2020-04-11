// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --cutoff-events=0 %t.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --thread-scheduling=last %t.bc 2>&1 | FileCheck --check-prefix CHECK-LAST %s
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --thread-scheduling=first %t.bc 2>&1 | FileCheck --check-prefix CHECK-FIRST %s

#include "klee/klee.h"

static klee_sync_primitive_t lock;

static uint8_t x = 0;

static void emit_events(void) {
  klee_lock_acquire(&lock);
  klee_lock_release(&lock);
}

static void thread(void* arg) {
  emit_events();

  if (x > 1) {
    // CHECK-DAG: larger than 1
    // CHECK-DAG: larger than 1
    klee_warning("larger than 1");
  }
}

int main(void) {
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x >= 0);

  klee_create_thread(thread, NULL);

  emit_events();

  if (x == 1) {
    // CHECK-DAG: equal to 1
    // CHECK-DAG: equal to 1
    klee_warning("equal to 1");
  }

  // CHECK-LAST: larger than 1
  // CHECK-LAST: equal to 1
  // CHECK-LAST: equal to 1

  // CHECK-FIRST: equal to 1
  // CHECK-FIRST: larger than 1
  // CHECK-FIRST: larger than 1

  return 0;
}
