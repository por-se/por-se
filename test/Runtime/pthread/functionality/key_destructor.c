// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

pthread_key_t key;
pthread_t mainThread;

int count = 0;

static void* test(void* arg) {
  pthread_setspecific(&key, NULL);
  return NULL;
}

static void destructor(void* keyValue) {
  void* v = pthread_getspecific(&key);
  assert(v != NULL);

  assert(pthread_equal(mainThread, pthread_self()));
  assert(keyValue == &count);

  assert(count < PTHREAD_DESTRUCTOR_ITERATIONS);

  pthread_setspecific(&key, &count);
  count++;
}

int main(void) {
  mainThread = pthread_self();
  pthread_key_create(&key, destructor);

  pthread_t thread;
  pthread_create(&thread, NULL, test, NULL);

  pthread_setspecific(&key, &count);

  void* v = pthread_getspecific(&key);
  assert(v == &count);

  pthread_join(thread, NULL);

  return 0;
}
