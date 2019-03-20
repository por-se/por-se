// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

int main(int argc, char **argv) {
  int rc;

  rc = pthread_rwlock_wrlock(&lock);
  assert(rc == 0);

  rc = pthread_rwlock_unlock(&lock);
  assert(rc == 0);

  return 0;
}