// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <fcntl.h>

sem_t sem;

static void* test(void* arg) {
  int rc;
  int v;
  sem_t* sem;

  sem = sem_open("/test", 0);
  assert(sem != SEM_FAILED);

  rc = sem_getvalue(sem, &v);
  assert(rc == 0);
  assert(v == 1);

  rc = sem_wait(sem);
  assert(rc == 0);

  rc = sem_getvalue(sem, &v);
  assert(rc == 0);
  assert(v == 0);

  return NULL;
}

int main(void) {
  int rc;
  sem_t* sem;

  sem = sem_open("/test", O_CREAT | O_EXCL, 0, 1);
  assert(sem != SEM_FAILED);

  pthread_t thread;
  pthread_create(&thread, NULL, test, NULL);

  pthread_join(thread, NULL);

  rc = sem_close(sem);
  assert(rc == 0);

  return 0;
}
