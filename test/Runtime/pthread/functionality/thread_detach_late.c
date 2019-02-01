// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

static void* test(void* arg) {
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1;

  pthread_create(&t1, NULL, test, NULL);
  assert(pthread_detach(t1) == 0);

  return 0;
}
