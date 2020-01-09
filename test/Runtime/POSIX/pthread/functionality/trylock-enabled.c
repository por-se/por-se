// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include "klee/klee.h"

#include <pthread.h>

#include <assert.h>
#include <stdbool.h>
#include <errno.h>

pthread_mutex_t mutex1;
pthread_mutex_t mutex2 = KPR_MUTEX_INITIALIZER_TRYLOCK;

void* thread(void* arg) {
  int ret1 = pthread_mutex_trylock(&mutex1);
  assert(ret1 == EBUSY);

  int ret2 = pthread_mutex_trylock(&mutex2);
  assert(ret2 == EBUSY);

  return NULL;
}

int main(void) {
  pthread_t th;
  pthread_mutexattr_t attr;

  pthread_mutexattr_init(&attr);
  kpr_pthread_mutexattr_settrylock(&attr, KPR_TRYLOCK_ENABLED);
  pthread_mutex_init(&mutex1, &attr);

  pthread_mutex_lock(&mutex1);
  pthread_mutex_lock(&mutex2);

  pthread_create(&th, NULL, thread, NULL);
  pthread_join(th, NULL);

  pthread_mutex_unlock(&mutex1);
  pthread_mutex_unlock(&mutex2);

  return 0;
}
