// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include "klee/klee.h"

#include <pthread.h>

#include <assert.h>
#include <stdbool.h>
#include <errno.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* thread(void* arg) {
  pthread_mutex_trylock(&mutex);
  return NULL;
}

int main(void) {
  pthread_t th;

  pthread_mutex_lock(&mutex);

  pthread_create(&th, NULL, thread, NULL);
  pthread_join(th, NULL);

  pthread_mutex_unlock(&mutex);

  return 0;
}