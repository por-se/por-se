// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <semaphore.h>
#include <assert.h>

sem_t sem;

static void* test(void* arg) {
  int rc;
  int v;

  rc = sem_getvalue(&sem, &v);
  assert(rc == 0);
  assert(v == 3);

  rc = sem_wait(&sem);
  assert(rc == 0);

  rc = sem_getvalue(&sem, &v);
  assert(rc == 0);
  assert(v == 2);

  rc = sem_trywait(&sem);
  assert(rc == 0);

  rc = sem_getvalue(&sem, &v);
  assert(rc == 0);
  assert(v == 1);

  rc = sem_wait(&sem);
  assert(rc == 0);

  rc = sem_getvalue(&sem, &v);
  assert(rc == 0);
  assert(v == 0);

  return NULL;
}

int main(void) {
  int rc;
  rc = sem_init(&sem, 0, 3);
  assert(rc == 0);

  pthread_t thread;
  pthread_create(&thread, NULL, test, NULL);

  pthread_join(thread, NULL);

  rc = sem_destroy(&sem);
  assert(rc == 0);

  return 0;
}
