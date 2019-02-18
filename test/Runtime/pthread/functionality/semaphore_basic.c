// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <pthread.h>
#include <semaphore.h>
#include <assert.h>

sem_t sem;

static void* test(void* arg) {
  int rc;

  rc = sem_wait(&sem);
  assert(rc == 0);

  return NULL;
}

int main(void) {
  int rc;
  rc = sem_init(&sem, 0, 0);
  assert(rc == 0);

  pthread_t thread;
  pthread_create(&thread, NULL, test, NULL);

  rc = sem_post(&sem);
  assert(rc == 0);

  pthread_join(thread, NULL);

  rc = sem_destroy(&sem);
  assert(rc == 0);

  return 0;
}
