// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

int main(int argc, char **argv) {
  int rc;

  rc = pthread_cond_broadcast(&cond);
  assert(rc == 0);

  return 0;
}