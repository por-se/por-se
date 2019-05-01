// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error --no-schedule-forks --log-por-events %t.bc 2>&1 | FileCheck %s

#include <pthread.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static pthread_t thread;

static void* threadFunc(void* arg) {
  pthread_mutex_lock(&mutex);

  pthread_cond_signal(&cond);

  pthread_mutex_unlock(&mutex);

  return NULL;
}

int main(void) {
  pthread_mutex_lock(&mutex);

  pthread_create(&thread, NULL, threadFunc, NULL);

  pthread_cond_wait(&cond, &mutex);

  pthread_mutex_unlock(&mutex);

  pthread_join(thread, NULL);

  return 0;
}


// CHECK: POR event: thread_init with current thread [[M_TID:[0-9]+]] and args: [[M_TID]]
// CHECK-NEXT: POR event: lock_acquire with current thread [[M_TID]] and args: [[LID:[0-9]+]]
// CHECK: POR event: thread_create with current thread [[M_TID]] and args: [[SEC_TID:[0-9]+]]
// CHECK-NEXT: POR event: wait1 with current thread [[M_TID]] and args: [[COND:[0-9]+]] [[LID]]

// CHECK-NEXT: POR event: lock_acquire with current thread [[SEC_TID]] and args: [[LID]]
// CHECK-NEXT: POR event: signal with current thread [[SEC_TID]] and args: [[COND]] [[M_TID]]
// CHECK-NEXT: POR event: lock_release with current thread [[SEC_TID]] and args: [[LID]]

// CHECK-NEXT: POR event: wait2 with current thread [[M_TID]] and args: [[COND]] [[LID]]
// CHECK-NEXT: POR event: lock_release with current thread [[M_TID]] and args: [[LID]]

// CHECK-NEXT: POR event: thread_exit with current thread [[SEC_TID]] and args: [[SEC_TID]]

// CHECK-NEXT: POR event: thread_join with current thread [[M_TID]] and args: [[SEC_TID]]
// CHECK-NEXT: POR event: thread_exit with current thread [[M_TID]] and args: [[M_TID]]