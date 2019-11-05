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

int rwlockInvalid;
pthread_rwlock_t rwlockZero = {0};
pthread_rwlock_t rwlockCorrect = PTHREAD_RWLOCK_INITIALIZER;

int main(int argc, char **argv) {
  pthread_rwlock_t rwlockRandom;

  assert(argc == 2);
  char mode = argv[1][0];

  // CHECK-NOT: KLEE: ERROR: {{.+}} check_memory_access: memory error{{$}}
  // CHECK-NOT: KLEE: ERROR: {{.+}} Trying to use an uninitialized pthread object{{$}}
  if (mode == 'i') {
    // CHECK_I: KLEE: ERROR: {{.+}} check_memory_access: memory error{{$}}
    pthread_rwlock_wrlock((pthread_rwlock_t*) &rwlockInvalid);
  } else if (mode == 'r') {
    // CHECK_R: KLEE: ERROR: {{.+}} Trying to use an uninitialized pthread object{{$}}
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
