// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error --no-schedule-forks --log-por-events %t.bc 2>&1 | FileCheck %s

#include <pthread.h>
#include <stdio.h>

int main(void) {
  // CHECK: POR event: thread_init with current thread [[M_TID:[0-9]+]] and args: [[M_TID]]
  pthread_mutex_t mutex;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  // CHECK-NEXT: POR event: lock_create with current thread [[M_TID]] and args: [[LID:[0-9]+]]
  pthread_mutex_init(&mutex, &attr);

  pthread_mutexattr_destroy(&attr);

  // CHECK-NEXT: POR event: lock_acquire with current thread [[M_TID]] and args: [[LID]]
  pthread_mutex_lock(&mutex);

  // CHECK-NOT: POR event: lock_acquire with current thread [[M_TID]] and args: [[LID]]
  pthread_mutex_lock(&mutex);

  // CHECK-NOT: POR event: lock_release with current thread [[M_TID]] and args: [[LID]]
  pthread_mutex_unlock(&mutex);

  // This is placed in between, so that we can differ between both releases
  // CHECK-NEXT: MARKER
  puts("MARKER");

  // CHECK-NEXT: POR event: lock_release with current thread [[M_TID]] and args: [[LID]]
  pthread_mutex_unlock(&mutex);

  // CHECK-NEXT: POR event: lock_destroy with current thread [[M_TID]] and args: [[LID]]
  pthread_mutex_destroy(&mutex);

  return 0;
  // CHECK-NEXT: POR event: thread_exit with current thread [[M_TID]] and args: [[M_TID]]
}
