// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --schedule-forks=sync-point --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

int _Atomic result = 0;

pthread_once_t once = PTHREAD_ONCE_INIT;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

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