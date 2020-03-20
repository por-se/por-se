// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <stdatomic.h>

static atomic_int counter = 0;

static void* thread(void* arg) {
  atomic_store(&counter, 1);
  return NULL;
}

int main (void) {
  pthread_t th;
  pthread_create(&th, NULL, thread, NULL);

  int v = atomic_load(&counter);

  pthread_join(th, NULL);

  return v;
}