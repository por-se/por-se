// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv) {
  int rc;

  rc = pthread_mutex_lock(&mutex);
  assert(rc == 0);

  rc = pthread_mutex_unlock(&mutex);
  assert(rc == 0);

  return 0;
}
