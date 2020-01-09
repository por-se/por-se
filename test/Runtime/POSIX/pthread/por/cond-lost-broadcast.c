// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error --log-por-events %t.bc 2>&1 | FileCheck %s

#include "klee/klee.h"

klee_sync_primitive_t cond;

int main(void) {
  // CHECK: POR event: thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]

  // CHECK: Starting test
  puts("Starting test");

  // CHECK-NEXT: POR event: broadcast with current thread [[M_TID]] on cond. var [[COND:[0-9]+]] and broadcasted threads:
  klee_cond_broadcast(&cond);

  return 0;
}
