// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t.bc 2>&1

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

static int staticObj;

bool isPageAligned(uint64_t addr) {
  long pageSize = sysconf(_SC_PAGE_SIZE);

  return (addr & (pageSize - 1)) == 0;
}

int main(int argc, char **argv) {
  void* smallObj = malloc(10);
  uint64_t addressSmallObj = (uint64_t) smallObj;
  free(smallObj);
  assert(isPageAligned(addressSmallObj));

  void* hugeObj = malloc(1024 * 1024 * 1024 * 4);
  uint64_t addressHugeObj = (uint64_t) hugeObj;
  free(hugeObj);
  assert(isPageAligned(addressHugeObj));

  uint64_t addressStack = (uint64_t) &smallObj;
  assert(isPageAligned(addressStack));

  uint64_t addressStatic = (uint64_t) &staticObj;
  assert(isPageAligned(addressStack));

  return 0;
}
