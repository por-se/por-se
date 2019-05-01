// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error --no-schedule-forks --log-por-events %t.bc 2>&1 | FileCheck %s

#include <pthread.h>

static void* noop(void* arg) {
  return NULL;
}

int main(void) {
  pthread_t thread;

  // CHECK: POR event: thread_init with current thread [[M_TID:[0-9]+]] and args: [[M_TID]]
  // This next check is not check-next, since there is a malloc line in between
  // CHECK: POR event: thread_create with current thread [[M_TID]] and args: [[SEC_TID:[0-9]+]]
  pthread_create(&thread, NULL, noop, NULL);

  // CHECK-NEXT: POR event: thread_exit with current thread [[SEC_TID]] and args: [[SEC_TID]]

  pthread_join(thread, NULL);

  // CHECK-NEXT: POR event: thread_join with current thread [[M_TID]] and args: [[SEC_TID]]
  // CHECK-NEXT: POR event: thread_exit with current thread [[M_TID]] and args: [[M_TID]]

  return 0;
}
