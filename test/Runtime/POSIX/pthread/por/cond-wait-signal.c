// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error --thread-scheduling=first --log-por-events %t.bc 2>&1 | FileCheck %s

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


// FIXME: this only tests with "first" thread scheduling as these checks rely on specific order of events

// CHECK: POR event: thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]
// CHECK-DAG: POR event: lock_acquire with current thread [[M_TID]] on mutex [[FS_LID:[0-9]+]]
// CHECK-DAG: POR event: lock_acquire with current thread [[M_TID]] on mutex [[LID:[0-9]+]]
// CHECK-DAG: POR event: thread_create with current thread [[M_TID]] and created thread [[SEC_TID:[0-9,]+]]
// CHECK-DAG: POR event: wait1 with current thread [[M_TID]] on cond. var [[COND:[0-9]+]] and mutex [[LID]]

// CHECK-DAG: POR event: thread_init with current thread [[SEC_TID]] and initialized thread [[SEC_TID]]
// CHECK-DAG: POR event: lock_acquire with current thread [[SEC_TID]] on mutex [[LID]]
// CHECK-DAG: POR event: signal with current thread [[SEC_TID]] on cond. var [[COND]] and signalled thread [[M_TID]]
// CHECK-DAG: POR event: lock_release with current thread [[SEC_TID]] on mutex [[LID]]

// CHECK-DAG: POR event: wait2 with current thread [[M_TID]] on cond. var [[COND]] and mutex [[LID]]
// CHECK-DAG: POR event: lock_release with current thread [[M_TID]] on mutex [[LID]]

// CHECK-DAG: POR event: thread_exit with current thread [[SEC_TID]] and exited thread [[SEC_TID]]

// CHECK-DAG: POR event: thread_join with current thread [[M_TID]] and joined thread [[SEC_TID]]
// CHECK-DAG: POR event: thread_exit with current thread [[M_TID]] and exited thread [[M_TID]]
