// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: %clang %s -emit-llvm %O0opt -g -c -DLOCK_NOTHING -o %t-lock.bc
// RUN: %clang %s -emit-llvm %O0opt -g -c -DPTHREAD_EXIT_FROM_MAIN -o %t-exit.bc
// RUN: rm -rf %t.klee-out
// RUN: rm -rf %t-lock.klee-out
// RUN: rm -rf %t-exit.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc
// RUN: %klee --output-dir=%t-lock.klee-out --posix-runtime --exit-on-error %t-lock.bc 2>&1 | FileCheck --check-prefix=CHECK-LOCK %s
// RUN: %klee --output-dir=%t-exit.klee-out --posix-runtime --exit-on-error %t-exit.bc

#include <pthread.h>
#include <stdio.h>

#define NUM_THREADS 2

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

void *thread_routine(void *arg) {
#ifdef LOCK_NOTHING
  pthread_mutex_lock(&m);
#endif
  puts("doing nothing, really.");
#ifdef LOCK_NOTHING
  pthread_mutex_unlock(&m);
#endif
  pthread_exit(0);
}

int main(int argc, char *argv[]) {
  pthread_t threads[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; ++i) {
    pthread_create(&threads[i], NULL, thread_routine, (void *)(size_t)i);
  }

  puts("exiting without waiting for any other threads");
#ifdef PTHREAD_EXIT_FROM_MAIN
  pthread_exit(0);
#else
  return 0;
#endif
}

// FIXME: is there a better way to check the paths - this number changes based on internal changes to the runtime
// CHECK-LOCK: KLEE: done: completed paths = 2
