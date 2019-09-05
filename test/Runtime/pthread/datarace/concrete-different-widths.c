// XFAIL: *
// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee  --pthread-runtime --output-dir=%t.klee-out %t.bc 2>&1
// RUN: test -f %t.klee-out/test000001.unsafememoryaccess.err

#include <pthread.h>
#include <assert.h>

#include <klee/klee.h>

int data[4] = { 0, 0 };

static void* test1(void* arg) {
  char* array = (char*) data;

  array[1] = 'a';

  return NULL;
}

static void* test2(void* arg) {
  int* array = (int*) data;

  array[0] = 1;

  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;
  pthread_attr_t attr;

  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&t1, &attr, test1, NULL);
  pthread_create(&t2, &attr, test2, NULL);

  return 0;
}
