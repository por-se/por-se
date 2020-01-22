// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --thread-scheduling=first --exit-on-error --debug-event-registration %t.bc 2>&1 | FileCheck %s

#include "klee/klee.h"

#include <stdio.h>

static void noop(void* arg) {
}

int main(void) {
  // CHECK: [state id: 0] registering thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]

  // CHECK-DAG: Starting test
  puts("Starting test");

  // CHECK-DAG: [state id: 0] registering thread_create with current thread [[M_TID]] and created thread [[SEC_TID:[0-9,]+]]
  klee_create_thread(noop, NULL);

  // CHECK-DAG: [state id: 0] registering thread_exit with current thread [[M_TID]] and exited thread [[M_TID]]

  // CHECK-DAG: [state id: 0] registering thread_init with current thread [[M_TID]] and initialized thread [[SEC_TID]]
  // CHECK-DAG: [state id: 0] registering thread_exit with current thread [[SEC_TID]] and exited thread [[SEC_TID]]

  return 0;
}
