// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error --no-schedule-forks --log-por-events %t.bc 2>&1 | FileCheck %s

#include <pthread.h>

int main(void) {
  // CHECK: POR event: thread_init with current thread 1 and args: 1
  pthread_cond_t cond;

  // CHECK-NEXT: POR event: condition_variable_create with current thread 1 and args: [[COND:[0-9]+]]
  pthread_cond_init(&cond, NULL);

  // CHECK-NEXT: POR event: condition_variable_destroy with current thread 1 and args: [[COND]]
  pthread_cond_destroy(&cond);

  return 0;
  // CHECK-NEXT: POR event: thread_exit with current thread 1 and args: 1
}
