// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime %t.bc 2>&1 | FileCheck %s
// RUN: test -f %t.klee-out/test000001.xxx.err

#include <pthread.h>
#include <assert.h>
#include <errno.h>

#include "klee/klee.h"

pthread_mutex_t mutex1, mutex2;

static void* func(void* arg) {
  int rc;

  rc = pthread_mutex_lock(&mutex1);
  assert(rc == 0);

  rc = pthread_mutex_lock(&mutex2);
  assert(rc == 0);

  return NULL;
}

int main(int argc, char **argv) {
  int rc = 0;
  pthread_t thread;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

  rc = pthread_mutex_init(&mutex1, &attr);
  assert(rc == 0);

  rc = pthread_mutex_init(&mutex2, &attr);
  assert(rc == 0);

  rc = pthread_mutexattr_destroy(&attr);
  assert(rc == 0);

  rc = pthread_create(&thread, NULL, func, NULL);
  assert(rc == 0);

  // After joining the thread is exited and should no longer be active
  pthread_join(thread, NULL);

  // CHECK: Reacquiring of robust mutex with owner being dead (unsupported)
  rc = pthread_mutex_lock(&mutex1);
  assert(rc == EOWNERDEAD);

  rc = pthread_mutex_lock(&mutex2);
  assert(rc == EOWNERDEAD);

  rc = pthread_mutex_consistent(&mutex1);
  assert(rc == 0);

  rc = pthread_mutex_unlock(&mutex1);
  assert(rc == 0);

  rc = pthread_mutex_unlock(&mutex2);
  assert(rc == 0);

  // Mutex 1 should be usable again while mutex2 should be unusable
  rc = pthread_mutex_lock(&mutex1);
  assert(rc == 0);

  rc = pthread_mutex_lock(&mutex2);
  assert(rc == EINVAL);

  return 0;
}
