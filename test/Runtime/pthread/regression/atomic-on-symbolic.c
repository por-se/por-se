// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include "klee/klee.h"

#include <assert.h>
#include <stdatomic.h>

int main() {
  atomic_int data1 = 0;
  atomic_int data2 = 0;

  int index = klee_int("index");
  klee_assume(index >= 0 & index <= 1);

  atomic_int* targets[] = { &data1, &data2 };

  int res = atomic_fetch_add(targets[index], 1);

  assert(data1 + data2 == 1 && res == 0);

  return 0;
}