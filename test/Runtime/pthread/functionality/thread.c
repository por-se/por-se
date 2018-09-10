// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

static void* test(void* arg) {
  int* num = (int*) arg;
  *num = (*num) * (*num);
  return arg;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;
  int n1 = 1;
  int n2 = 2;

  pthread_create(&t1, NULL, test, (void*) &n1);
  pthread_create(&t2, NULL, test, (void*) &n2);

  void* retPtr1 = (void*) NULL;
  void* retPtr2 = (void*) NULL;

  pthread_join(t1, &retPtr1);
  pthread_join(t2, &retPtr2);

  assert(n1 == 1 && n2 == 4);
  assert(retPtr1 == &n1 && retPtr2 == &n2);

  return 0;
}
