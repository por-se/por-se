// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc i 2>&1 | FileCheck -check-prefix=CHECK_I %s
// RUN: test -f %t.klee-out/test000001.ptr.err
// RUN: rm -rf %t.klee-out
// RUN: not %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc r 2>&1 | FileCheck -check-prefix=CHECK_R %s
// RUN: test -f %t.klee-out/test000001.user
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc z 2>&1 | FileCheck %s
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc c 2>&1 | FileCheck %s

#include <pthread.h>
#include <assert.h>

int condInvalid;
pthread_cond_t condZero = {0};
pthread_cond_t condCorrect = PTHREAD_COND_INITIALIZER;

int main(int argc, char **argv) {
  pthread_cond_t condRandom;

  assert(argc == 2);
  char mode = argv[1][0];

  // CHECK-NOT: KLEE: ERROR: {{.+}} check_memory_access: memory error{{$}}
  // CHECK-NOT: KLEE: ERROR: {{.+}} Trying to use an uninitialized pthread object{{$}}
  if (mode == 'i') {
    // CHECK_I: KLEE: ERROR: {{.+}} check_memory_access: memory error{{$}}
    pthread_cond_broadcast((pthread_cond_t*) &condInvalid);
  } else if (mode == 'r') {
    // CHECK_R: KLEE: ERROR: {{.+}} Trying to use an uninitialized pthread object{{$}}
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
