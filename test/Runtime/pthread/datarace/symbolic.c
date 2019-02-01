// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee  --pthread-runtime --output-dir=%t.klee-out %t.bc 2>&1
// RUN: test -f %t.klee-out/test000001.unsafememoryaccess.err

#include <pthread.h>
#include <assert.h>

#include <klee/klee.h>

static int num[3];

static void* test(void* arg) {
  int* refIndex = (int*) arg;
  num[*refIndex]++;
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  int index1 = klee_int("index1");
  int index2 = klee_int("index2");
  klee_assume(index1 >= 0 & index1 <= 1);
  klee_assume(index2 >= 1 & index2 <= 2);

  pthread_create(&t1, NULL, test, &index1);
  pthread_create(&t2, NULL, test, &index2);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(0);

  return 0;
}
