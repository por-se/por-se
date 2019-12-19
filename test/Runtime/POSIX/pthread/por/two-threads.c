// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --thread-scheduling=first --posix-runtime --exit-on-error --log-por-events %t.bc 2>&1 | FileCheck %s

#include <pthread.h>

static void* noop(void* arg) {
  return NULL;
}

int main(void) {
  pthread_t thread;

  // CHECK: POR event: thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]
  // CHECK-DAG: POR event: lock_acquire with current thread [[M_TID]] on mutex [[FS_LID:[0-9]+]]
  // This next check is not check-next, since there is a malloc line in between
  // CHECK-DAG: POR event: thread_create with current thread [[M_TID]] and created thread [[SEC_TID:[0-9,]+]]
  pthread_create(&thread, NULL, noop, NULL);


  pthread_join(thread, NULL);

  // CHECK-DAG: POR event: thread_init with current thread [[SEC_TID]] and initialized thread [[SEC_TID]]
  // CHECK-DAG: POR event: thread_exit with current thread [[SEC_TID]] and exited thread [[SEC_TID]]
  // CHECK-DAG: POR event: thread_join with current thread [[M_TID]] and joined thread [[SEC_TID]]
  // CHECK-DAG: POR event: thread_exit with current thread [[M_TID]] and exited thread [[M_TID]]

  return 0;
}
