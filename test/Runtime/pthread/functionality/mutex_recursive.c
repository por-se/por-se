// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

#include "klee/klee.h"

pthread_mutex_t mutex;

int number = 0;

static void* f1(void* arg) {
  int rc;

  rc = pthread_mutex_lock(&mutex);
  assert(rc == 0);
  assert(number == 1);

  number++;

  rc = pthread_mutex_lock(&mutex);
  assert(rc == 0);
  assert(number == 2);

  number++;

  rc = pthread_mutex_unlock(&mutex);
  assert(rc == 0);
  assert(number == 3);

  number++;

  rc = pthread_mutex_unlock(&mutex);
  assert(rc == 0);
  assert(number == 4);

  return NULL;
}

int main(int argc, char **argv) {
  int rc = 0;
  pthread_t thread;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  rc = pthread_mutex_init(&mutex, &attr);
  assert(rc == 0);

  rc = pthread_mutexattr_destroy(&attr);
  assert(rc == 0);

  rc = pthread_mutex_lock(&mutex);
  assert(rc == 0);

  pthread_create(&thread, NULL, f1, NULL);

  assert(number == 0);

  rc = pthread_mutex_lock(&mutex);
  assert(rc == 0);
  assert(number == 0);

  number++;

  rc = pthread_mutex_unlock(&mutex);
  assert(rc == 0);
  assert(number == 1);

  rc = pthread_mutex_unlock(&mutex);
  assert(rc == 0);

  pthread_join(thread, NULL);

  assert(number == 4);

  return 0;
}
