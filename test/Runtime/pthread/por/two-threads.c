// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error --no-schedule-forks --log-por-events %t.bc 2>&1 | FileCheck %s

#include <pthread.h>

static void* noop(void* arg) {
  return NULL;
}

int main(void) {
  pthread_t thread;

  // CHECK: POR event: thread_init with current thread 1 and args: 1
  // This next check is not check-next, since there is a malloc line in between
  // CHECK: POR event: thread_create with current thread 1 and args: 2
  pthread_create(&thread, NULL, noop, NULL);

  // CHECK-NEXT: POR event: thread_exit with current thread 2 and args: 2

  pthread_join(thread, NULL);

  // CHECK-NEXT: POR event: thread_join with current thread 1 and args: 2
  // CHECK-NEXT: POR event: thread_exit with current thread 1 and args: 1

  return 0;
}
