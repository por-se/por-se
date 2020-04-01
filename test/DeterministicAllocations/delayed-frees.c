// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --posix-runtime --allocate-quarantine=0 %t.bc 2>&1

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define CHECK_COUNT (64)

void* addresses[CHECK_COUNT];

void* thread(void* arg) {
  free(arg);

  return NULL;
}

bool check_for_address(void* a) {
  bool result = false;

  uintptr_t vA = (uintptr_t)a;

  for (int i = 0; i < CHECK_COUNT; i++) {
    addresses[i] = malloc(4);
    result |= (((uintptr_t) addresses[i]) == vA);
  }

  for (int i = 0; i < CHECK_COUNT; i++) {
    free(addresses[i]);
  }

  return result;
}

int main (void) {
  void* a = malloc(4);

  pthread_t th;
  pthread_create(&th, NULL, thread, a);

  assert(!check_for_address(a));

  pthread_join(th, NULL);

  assert(check_for_address(a));

  return 0;
}
