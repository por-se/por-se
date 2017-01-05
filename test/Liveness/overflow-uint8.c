// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -output-dir=%t-O0.klee-out -detect-infinite-loops %t-O0.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -output-dir=%t-O1.klee-out -detect-infinite-loops %t-O1.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O1.klee-out/test000001.infty.err
// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -output-dir=%t-O2.klee-out -detect-infinite-loops %t-O2.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O2.klee-out/test000001.infty.err
// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -output-dir=%t-O3.klee-out -detect-infinite-loops %t-O3.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O3.klee-out/test000001.infty.err

#include <inttypes.h>
#include <stdio.h>

#include <klee/klee.h>

int main(void) {
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x < 0);

  // CHECK: infinite loop
  for (uint8_t i = 0; i != x; i++) {
    printf("%d\n", i);
  }
}