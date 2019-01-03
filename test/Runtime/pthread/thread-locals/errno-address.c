// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <assert.h>
#include <pthread.h>
#include <errno.h>

static void* threadFunc(void* arg) {
  int* address = &errno;
  return address;
}

int main(void) {
  pthread_t th1, th2;

  pthread_create(&th1, NULL, threadFunc, NULL);
  pthread_create(&th2, NULL, threadFunc, NULL);

  int* addressMt = &errno;
  int* addressTh1 = NULL;
  int* addressTh2 = NULL;

  pthread_join(th1, (void**) &addressTh1);
  pthread_join(th2, (void**) &addressTh2);

  assert(addressMt != addressTh1);
  assert(addressMt != addressTh2);
  assert(addressTh1 != addressTh2);

  return 0;
}
