// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t-main.klee-out
// RUN: rm -rf %t-child.klee-out
// RUN: %klee --output-dir=%t-main.klee-out --exit-on-error --thread-scheduling=first --log-por-events %t.bc 2>&1 | FileCheck --check-prefix=CHECK-MAIN %s
// RUN: %klee --output-dir=%t-child.klee-out --exit-on-error --thread-scheduling=first --log-por-events %t.bc 2>&1 | FileCheck --check-prefix=CHECK-CHILD %s

#include "klee/klee.h"

#include <stdio.h>

static klee_sync_primitive_t lock;
static klee_sync_primitive_t cond;

static void thread(void* arg) {
  // CHECK-CHILD: POR event: thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]

  // CHECK-CHILD-DAG: Starting test

  // CHECK-CHILD-DAG: POR event: thread_create with current thread [[M_TID]] and created thread [[SEC_TID:[0-9,]+]]
  // CHECK-CHILD-DAG: POR event: thread_init with current thread [[SEC_TID]] and initialized thread [[SEC_TID]]

  // CHECK-CHILD-DAG: POR event: lock_acquire with current thread [[SEC_TID]] on mutex [[LID:[0-9]+]]
  klee_lock_acquire(&lock);

  // CHECK-CHILD-DAG: POR event: broadcast with current thread [[SEC_TID]] on cond. var [[COND:[0-9]+]] and broadcasted threads: [[M_TID]]
  klee_cond_broadcast(&cond);

  // CHECK-CHILD-DAG: POR event: lock_release with current thread [[SEC_TID]] on mutex [[LID]]
  klee_lock_release(&lock);

  // CHECK-CHILD-DAG: POR event: thread_exit with current thread [[SEC_TID]] and exited thread [[SEC_TID]]
}

int main(void) {
  // CHECK-MAIN: POR event: thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]

  // CHECK-MAIN-DAG: Starting test
  puts("Starting test");

  // CHECK-MAIN-DAG: POR event: lock_acquire with current thread [[M_TID]] on mutex [[LID:[0-9]+]]
  klee_lock_acquire(&lock);

  // CHECK-MAIN-DAG: POR event: thread_create with current thread [[M_TID]] and created thread [[SEC_TID:[0-9,]+]]
  klee_create_thread(&thread, NULL);

  // CHECK-MAIN-DAG: POR event: wait1 with current thread [[M_TID]] on cond. var [[COND:[0-9]+]] and mutex [[LID]]
  klee_cond_wait(&cond, &lock);
  // CHECK-MAIN-DAG: POR event: broadcast with current thread [[SEC_TID]] on cond. var [[COND]] and broadcasted threads: [[M_TID]]
  // CHECK-MAIN-DAG: POR event: wait2 with current thread [[M_TID]] on cond. var [[COND]] and mutex [[LID]]

  // CHECK-MAIN-DAG: POR event: lock_release with current thread [[M_TID]] on mutex [[LID]] 
  klee_lock_release(&lock);

  // CHECK-MAIN-DAG: POR event: thread_exit with current thread [[M_TID]] and exited thread [[M_TID]]
  return 0;
}
