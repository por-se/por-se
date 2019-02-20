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

int condInvalid;
pthread_cond_t condZero = {0};
pthread_cond_t condCorrect = PTHREAD_COND_INITIALIZER;

int main(int argc, char **argv) {
  pthread_cond_t condRandom;

  assert(argc == 2);
  char mode = argv[1][0];

  if (mode == 'i') {
    pthread_cond_broadcast((pthread_cond_t*) &condInvalid);
  } else if (mode == 'r') {
    pthread_cond_broadcast(&condRandom);
  } else if (mode == 'z') {
    pthread_cond_broadcast(&condZero);
  } else if (mode == 'c') {
    pthread_cond_broadcast(&condCorrect);
  } else {
    assert(0);
  }

  return 0;
}