// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
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

int mutexInvalid;
pthread_mutex_t mutexZero = {0};
pthread_mutex_t mutexCorrect = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv) {
  pthread_mutex_t mutexRandom;

  assert(argc == 2);
  char mode = argv[1][0];

  if (mode == 'i') {
    pthread_mutex_lock((pthread_mutex_t*) &mutexInvalid);
  } else if (mode == 'r') {
    pthread_mutex_lock(&mutexRandom);
  } else if (mode == 'z') {
    pthread_mutex_lock(&mutexZero);
  } else if (mode == 'c') {
    pthread_mutex_lock(&mutexCorrect);
  } else {
    assert(0);
  }

  return 0;
}