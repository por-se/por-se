// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>

pthread_barrier_t barrier;

static void* threadFunc(void* arg) {
  log(1);
  assert(errno == 0);

  pthread_barrier_wait(&barrier);

  log(-10);
  assert(errno != 0);

  return NULL;
}

int main(void) {
  pthread_barrier_init(&barrier, NULL, 2);

  pthread_t thread;
  pthread_create(&thread, NULL, threadFunc, NULL);

  log(-10);
  assert(errno != 0);

  pthread_barrier_wait(&barrier);

  assert(errno != 0);

  pthread_join(thread, NULL);

  return 0;
}
