// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <assert.h>
#include <pthread.h>
#include <errno.h>

pthread_barrier_t barrier;

static void* threadFunc(void* arg) {
  errno = 10;

  pthread_barrier_wait(&barrier);

  assert(errno == 10);
  errno = 11;

  pthread_barrier_wait(&barrier);

  assert(errno == 11);
  errno = 12;

  pthread_barrier_wait(&barrier);

  assert(errno == 12);
  errno = 13;

  pthread_barrier_wait(&barrier);

  assert(errno == 13);
  errno = 14;

  return NULL;
}

int main(void) {
  pthread_barrier_init(&barrier, NULL, 2);

  errno = 1;

  pthread_t thread;
  pthread_create(&thread, NULL, threadFunc, NULL);

  assert(errno == 1);
  errno = 2;

  pthread_barrier_wait(&barrier);

  assert(errno == 2);
  errno = 3;

  pthread_barrier_wait(&barrier);

  assert(errno == 3);
  errno = 4;

  pthread_barrier_wait(&barrier);

  assert(errno == 4);
  errno = 5;

  pthread_barrier_wait(&barrier);

  assert(errno == 5);
  errno = 6;

  pthread_join(thread, NULL);

  assert(errno == 6);

  return 0;
}
