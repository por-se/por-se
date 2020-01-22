// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t.bc 2>&1

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  uint64_t firstAddress = 0;
  uint64_t newAddress = 0;

  void* obj = malloc(10);
  firstAddress = (uint64_t) obj;
  free(obj);

  obj = malloc(10);
  newAddress = (uint64_t) obj;
  free(obj);

  assert(firstAddress == newAddress);

  return 0;
}
