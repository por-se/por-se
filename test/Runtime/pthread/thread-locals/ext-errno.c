// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>

pthread_barrier_t barrier;

static void* threadFunc(void* arg) {
  errno = 0;

  log(1);
  assert(errno == 0);

  pthread_barrier_wait(&barrier);

  log(-10);
  assert(errno != 0);

  return NULL;
}

int main(void) {
  assert(errno == 0);

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
