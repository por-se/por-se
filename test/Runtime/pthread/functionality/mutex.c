// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

#include "klee/klee.h"

pthread_mutex_t mutex;

int number = 0;

static void* f1(void* arg) {
  pthread_mutex_lock(&mutex);
  number++;
  pthread_mutex_unlock(&mutex);

  return NULL;
}

static void* f2(void* arg) {
  pthread_mutex_lock(&mutex);
  number++;
  pthread_mutex_unlock(&mutex);

  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_mutex_init(&mutex, NULL);

  pthread_create(&t1, NULL, f1, NULL);
  pthread_create(&t2, NULL, f2, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(number == 2);

  return 0;
}
