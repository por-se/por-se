// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --pthread-runtime --exit-on-error %t1.bc

#include <pthread.h>
#include <assert.h>

pthread_key_t key;
pthread_t mainThread;

int mainThreadNumber;
int childThreadNumber;

static void* test(void* arg) {
  pthread_setspecific(&key, &childThreadNumber);

  void* v = pthread_getspecific(&key);
  assert(v == &childThreadNumber);

  return NULL;
}

static void destructor(void* keyValue) {
  void* v = pthread_getspecific(&key);
  assert(v == NULL);

  if (pthread_equal(mainThread, pthread_self())) {
    assert(keyValue == &mainThreadNumber);
  } else {
    assert(keyValue == &childThreadNumber);
  }
}

int main(void) {
  mainThread = pthread_self();
  pthread_key_create(&key, destructor);

  pthread_t thread;
  pthread_create(&thread, NULL, test, NULL);

  pthread_setspecific(&key, &mainThreadNumber);

  void* v = pthread_getspecific(&key);
  assert(v == &mainThreadNumber);

  pthread_join(thread, NULL);

  return 0;
}
