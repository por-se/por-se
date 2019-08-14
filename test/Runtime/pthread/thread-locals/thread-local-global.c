// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <assert.h>
#include <pthread.h>

static _Thread_local int target1 = 0;
static _Thread_local int target2 = 42;

void* addressOfTarget1 = NULL;
void* addressOfTarget2 = NULL;

static void* threadFunc(void* arg) {
  assert(target1 == 0);
  assert(target2 == 42);

  target1 = 1;
  target2 = 0;

  printf("IN   T1: %p + %p \n", &target1, &target2);
  printf("FROM MT: %p + %p \n", addressOfTarget1, addressOfTarget2);

  assert(addressOfTarget1 != NULL && addressOfTarget1 != &target1);
  assert(addressOfTarget2 != NULL && addressOfTarget2 != &target2);

  return NULL;
}

int main(void) {
  assert(target1 == 0);
  assert(target2 == 42);

  target1 = 200;
  target2 = 201;

  addressOfTarget1 = &target1;
  addressOfTarget2 = &target2;

  printf("IN   MT: %p + %p \n", &target1, &target2);
  printf("FROM MT: %p + %p \n", addressOfTarget1, addressOfTarget2);

  pthread_t th;
  assert(pthread_create(&th, NULL, threadFunc, NULL) == 0);

  assert(target1 == 200);
  assert(target2 == 201);

  target1 = 100;
  target2 = 101;

  assert(pthread_join(th, NULL) == 0);

  assert(target1 == 100);
  assert(target2 == 101);

  printf("IN   MT: %p + %p \n", &target1, &target2);
  printf("FROM MT: %p + %p \n", addressOfTarget1, addressOfTarget2);

  return 0;
}
