// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t.bc 2>&1

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static long pageSize;

static int staticObj1;
static int staticObj2;

void testStaticPlacing() {
  uint64_t address1 = (uint64_t) &staticObj1;
  uint64_t address2 = (uint64_t) &staticObj2;

  assert(address1 != address2);

  if (address1 < address2) {
    assert((address2 - address1) >= pageSize);
  } else {
    assert((address1 - address2) >= pageSize);
  }
}

void testHeapPlacing() {
  void* obj1 = malloc(10);
  uint64_t address1 = (uint64_t) obj1;
  free(obj1);

  void* obj2 = malloc(10);
  uint64_t address2 = (uint64_t) obj2;
  free(obj2);

  assert(address1 != address2);

  if (address1 < address2) {
    assert((address2 - address1) >= pageSize);
  } else {
    assert((address1 - address2) >= pageSize);
  }
}

void testStackPlacing() {
  int obj1 = 1;
  int obj2 = 2;

  uint64_t address1 = (uint64_t) &obj1;
  uint64_t address2 = (uint64_t) &obj2;

  assert(address1 != address2);

  if (address1 < address2) {
    assert((address2 - address1) >= pageSize);
  } else {
    assert((address1 - address2) >= pageSize);
  }
}

int main(int argc, char **argv) {
  pageSize = sysconf(_SC_PAGE_SIZE);

  testHeapPlacing();
  testStackPlacing();

  return 0;
}
