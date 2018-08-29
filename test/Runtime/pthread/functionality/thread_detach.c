// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime -fork-on-thread-scheduling --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

static void* test(void* arg) {
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_create(&t1, NULL, test, NULL);
  pthread_create(&t2, NULL, test, NULL);

  // If the threads will not exit, then klee will report and error

  return 0;
}
