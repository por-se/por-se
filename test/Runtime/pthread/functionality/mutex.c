// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

#include "klee/klee.h"

pthread_mutex_t mutex;

int number = 0;

static void* f1(void* arg) {
  int val = 0;
  do {
    pthread_mutex_lock(&mutex);
    if ((number % 2) == 0) {
      number++;
    }

    val = number;
    pthread_mutex_unlock(&mutex);
  } while(val < 10);

  return NULL;
}

static void* f2(void* arg) {
  int val = 0;
  do {
    pthread_mutex_lock(&mutex);
    if ((number % 2) == 1) {
      number++;
    }

    val = number;
    pthread_mutex_unlock(&mutex);
  } while(val < 10);

  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_mutex_init(&mutex, NULL);

  pthread_create(&t1, NULL, f1, NULL);
  pthread_create(&t2, NULL, f2, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(number >= 10);

  return 0;
}
