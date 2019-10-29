// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error --log-por-events %t.bc 2>&1 | FileCheck %s

#include <pthread.h>

int main(void) {
  // CHECK: POR event: thread_init with current thread [[M_TID:tid<[0-9,]+>]] and initialized thread [[M_TID]]
  // CHECK-DAG: POR event: lock_acquire with current thread [[M_TID]] on mutex [[FS_LID:[0-9]+]]
  pthread_cond_t cond;

  // CHECK-DAG: POR event: condition_variable_create with current thread [[M_TID]] on cond. var [[COND:[0-9]+]]
  pthread_cond_init(&cond, NULL);

  // CHECK-NEXT: POR event: condition_variable_destroy with current thread [[M_TID]] on cond. var [[COND]]
  pthread_cond_destroy(&cond);

  return 0;
  // CHECK-NEXT: POR event: thread_exit with current thread [[M_TID]] and exited thread [[M_TID]]
}
