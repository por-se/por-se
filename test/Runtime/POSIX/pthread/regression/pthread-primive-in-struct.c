// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

typedef struct {
  pthread_mutex_t mutex;
  int a;
} test_1_t;

typedef struct {
  int a;
  pthread_mutex_t mutex;
} test_2_t;

int main(int argc, char** argv) {
  test_1_t test1;
  test_2_t test2;

  int r;

  r = pthread_mutex_init(&test1.mutex, NULL);
  assert(r == 0);
  r = pthread_mutex_lock(&test1.mutex);
  assert(r == 0);
  r = pthread_mutex_unlock(&test1.mutex);
  assert(r == 0);
  r = pthread_mutex_destroy(&test1.mutex);

  r = pthread_mutex_init(&test2.mutex, NULL);
  assert(r == 0);
  r = pthread_mutex_lock(&test2.mutex);
  assert(r == 0);
  r = pthread_mutex_unlock(&test2.mutex);
  assert(r == 0);
  r = pthread_mutex_destroy(&test2.mutex);
  assert(r == 0);

  return 0;
}
