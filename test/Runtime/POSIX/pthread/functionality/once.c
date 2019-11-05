// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

int _Atomic result = 0;

pthread_once_t once = PTHREAD_ONCE_INIT;

void onceFunction(void) {
  __c11_atomic_fetch_add(&result, 1, 0);
}

void* ThreadEntry(void* id) {
  pthread_once(&once, onceFunction);
  return NULL;
}

int main(void) {
  pthread_t t1, t2;

  pthread_create(&t1, NULL, ThreadEntry, NULL);
  pthread_create(&t2, NULL, ThreadEntry, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(result == 1 && "Should only be called once");

  return 0;
}
