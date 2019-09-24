// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

struct data {
  int a;
  int b;
};

struct data _data;

static void* thread(void* arg) {
  _data.a = 1;
  return 0;
}

int main(int argc, char** argv) {
  pthread_t th;

  pthread_create(&th, NULL, thread, NULL);

  _data.b = 1;

  pthread_join(th, NULL);

  assert(_data.a == 1 && _data.b == 1);

  return 0;
}