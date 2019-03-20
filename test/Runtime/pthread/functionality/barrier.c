// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

#include <stdatomic.h>

#include "klee/klee.h"

pthread_barrier_t barrier;

atomic_int number1 = 0;
atomic_int number2 = 0;

static void* func(void* arg) {
  atomic_int* target = (atomic_int*) arg;

  int n1, n2;

  atomic_fetch_add(target, 1);

  pthread_barrier_wait(&barrier);

  n1 = atomic_load(&number1);
  n2 = atomic_load(&number2);

  assert(n1 == 1 && n2 == 1 && "Should be impossible to process beyond barriers");

  pthread_barrier_wait(&barrier);

  atomic_fetch_add(target, 1);

  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_barrier_init(&barrier, NULL, 2);

  pthread_create(&t1, NULL, func, &number1);
  pthread_create(&t2, NULL, func, &number2);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(number1 == 2 && number2 == 2 && "Should be impossible to process beyond barriers");

  return 0;
}
