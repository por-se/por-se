// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc i 2>&1 | FileCheck -check-prefix=CHECK_I %s
// RUN: test -f %t.klee-out/test000001.ptr.err
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc r 2>&1 | FileCheck -check-prefix=CHECK_R %s
// RUN: test -f %t.klee-out/test000001.user
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc z 2>&1 | FileCheck %s
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc c 2>&1 | FileCheck %s

#include <pthread.h>
#include <assert.h>

int mutexInvalid;
pthread_mutex_t mutexZero = {0};
pthread_mutex_t mutexCorrect = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv) {
  pthread_mutex_t mutexRandom;

  assert(argc == 2);
  char mode = argv[1][0];

  // CHECK-NOT: KLEE: ERROR: {{.+}} check_memory_access: memory error{{$}}
  // CHECK-NOT: KLEE: ERROR: {{.+}} Trying to use an uninitialized pthread object{{$}}
  if (mode == 'i') {
    // CHECK_I: KLEE: ERROR: {{.+}} check_memory_access: memory error{{$}}
    pthread_mutex_lock((pthread_mutex_t*) &mutexInvalid);
  } else if (mode == 'r') {
    // CHECK_R: KLEE: ERROR: {{.+}} Trying to use an uninitialized pthread object{{$}}
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
