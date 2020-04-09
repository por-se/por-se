// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: %clang %s -emit-llvm %O0opt -g -c -DLOCK_NOTHING -o %t-lock.bc
// RUN: rm -rf %t.klee-out
// RUN: rm -rf %t-lock.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc 2>&1 | FileCheck %s
// RUN: %klee --output-dir=%t-lock.klee-out --posix-runtime --exit-on-error %t-lock.bc 2>&1 | FileCheck %s

#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

void *thread1(void *arg) {
#ifdef LOCK_NOTHING
  pthread_mutex_lock(&m);
#endif
  
  puts("work thread 1");
  // CHECK: work thread 1

#ifdef LOCK_NOTHING
  pthread_mutex_unlock(&m);
#endif

  exit(0);
  return NULL;
}

void *thread2(void *arg) {
#ifdef LOCK_NOTHING
  pthread_mutex_lock(&m);
#endif
  
  puts("work thread 2");
  // CHECK: work thread 2

#ifdef LOCK_NOTHING
  pthread_mutex_unlock(&m);
#endif

  exit(0);
  return NULL;
}

int main(int argc, char *argv[]) {
  pthread_t th1, th2;

  pthread_create(&th1, NULL, thread1, NULL);
  pthread_create(&th2, NULL, thread2, NULL);

  pthread_join(th1, NULL);
  pthread_join(th2, NULL);

  return -1;
}