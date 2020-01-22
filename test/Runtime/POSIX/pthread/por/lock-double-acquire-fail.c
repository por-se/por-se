// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error --debug-event-registration %t.bc 2>&1 | FileCheck %s

#include "klee/klee.h"

klee_sync_primitive_t lock;

int main(void) {
  // CHECK: [state id: 0] registering thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]

  // CHECK: Starting test
  puts("Starting test");

  // CHECK-DAG: [state id: 0] registering lock_acquire with current thread [[M_TID]] on mutex [[LID:[0-9]+]]
  klee_lock_acquire(&lock);

  klee_lock_acquire(&lock);

  // CHECK-NOT: UNREACHABLE
  puts("UNREACHABLE");

  return 0;
}
