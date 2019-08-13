// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --allocate-thread-heap-size=50 --allocate-thread-segments-file=%p/thread-mappings.conf %t.bc 2>&1 | FileCheck %s

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

void* thread(void* arg) {
  void* obj = malloc(10);
  uint64_t address = (uint64_t) obj;
  free(obj);

  assert(address >= 0x87c30000000 && address <= (0x87c30000000 + (50 * 1024 * 1024 * 1024)));

  return NULL;
}

int main(int argc, char **argv) {
  // CHECK: KLEE: Created thread memory mapping for tid<1> at 0x7ff30000000
  // CHECK: KLEE: Created thread memory mapping for tid<1,1> at 0x87c30000000

  void* obj = malloc(10);
  uint64_t address = (uint64_t) obj;
  free(obj);

  assert(address >= 0x7ff30000000 && address <= (0x7ff30000000 + (50 * 1024 * 1024 * 1024)));

  pthread_t pthread;
  int rc;

  assert(pthread_create(&pthread, NULL, thread, NULL) == 0);
  assert(pthread_join(pthread, NULL) == 0);

  return 0;
}
