// REQUIRES: posix-runtime
// RUN: %clang %s -emit-llvm %O0opt -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --posix-runtime --optimize --libc=uclibc %t1.bc

#include <string.h>
#include <assert.h>

#include "klee/klee.h"

int main() {
  int a;

  klee_make_symbolic(&a, sizeof a, "a");

  memset(&a, 0, sizeof a);

  if (a) {
    assert(0);
  }
  
  return 0;
}
