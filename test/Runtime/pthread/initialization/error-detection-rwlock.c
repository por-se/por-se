// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc i
// RUN: test -f %t.klee-out/test000001.user
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc r
// RUN: test -f %t.klee-out/test000001.user
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc z
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc c

#include <pthread.h>
#include <assert.h>

int rwlockInvalid;
pthread_rwlock_t rwlockZero = {0};
pthread_rwlock_t rwlockCorrect = PTHREAD_RWLOCK_INITIALIZER;

int main(int argc, char **argv) {
  pthread_rwlock_t rwlockRandom;

  assert(argc == 2);
  char mode = argv[1][0];

  if (mode == 'i') {
    pthread_rwlock_wrlock((pthread_rwlock_t*) &rwlockInvalid);
  } else if (mode == 'r') {
    pthread_rwlock_wrlock(&rwlockRandom);
  } else if (mode == 'z') {
    pthread_rwlock_wrlock(&rwlockZero);
  } else if (mode == 'c') {
    pthread_rwlock_wrlock(&rwlockCorrect);
  } else {
    assert(0);
  }

  return 0;
}