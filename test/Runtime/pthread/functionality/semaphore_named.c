// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <fcntl.h>

sem_t sem;

static void* test(void* arg) {
  int rc;
  sem_t* sem;

  sem = sem_open("42", 0);
  assert(sem == SEM_FAILED);

  sem = sem_open("/test", O_EXCL | O_CREAT, 0, 0);
  assert(sem == SEM_FAILED);

  sem = sem_open("/test", 0);
  assert(sem != SEM_FAILED);

  rc = sem_wait(sem);
  assert(rc == 0);

  rc = sem_wait(sem);
  assert(rc == 0);

  return NULL;
}

int main(void) {
  int rc;
  sem_t* sem;

  sem = sem_open("/test", O_CREAT | O_EXCL, 0, 0);
  assert(sem != SEM_FAILED);

  sem_t* dup;
  dup = sem_open("/test", 0);
  assert(dup != SEM_FAILED);

  pthread_t thread;
  pthread_create(&thread, NULL, test, NULL);

  rc = sem_post(sem);
  assert(rc == 0);

  rc = sem_post(dup);
  assert(rc == 0);

  pthread_join(thread, NULL);

  rc = sem_close(sem);
  assert(rc == 0);

  return 0;
}
