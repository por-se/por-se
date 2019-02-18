// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

pthread_key_t key;

static void* test(void* arg) {
  pthread_setspecific(&key, &key);

  pthread_key_delete(&key);

  return NULL;
}

static void destructor(void* keyValue) {
  // This function should not be called!
  assert(0);
}

int main(void) {
  pthread_key_create(&key, destructor);
  pthread_setspecific(&key, &key);

  pthread_t thread;
  pthread_create(&thread, NULL, test, NULL);
  pthread_join(thread, NULL);

  return 0;
}
