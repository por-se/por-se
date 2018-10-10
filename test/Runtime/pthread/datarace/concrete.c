// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee  --pthread-runtime --output-dir=%t.klee-out %t2.bc 2>&1
// RUN: test -f %t.klee-out/test000001.unsafememoryaccess.err

#include <pthread.h>
#include <assert.h>

static int num = 0;

static void* test(void* arg) {
  num++;
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_create(&t1, NULL, test, NULL);
  pthread_create(&t2, NULL, test, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(0);

  return 0;
}
