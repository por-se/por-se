// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

#include <semaphore.h>
#include <assert.h>
#include <errno.h>

int main(void) {
  int rc;
  int v;

  sem_t sem;

  rc = sem_init(&sem, 0, SEM_VALUE_MAX);
  assert(rc == 0);

  rc = sem_getvalue(&sem, &v);
  assert(rc == 0);
  assert(v == SEM_VALUE_MAX);

  errno = 0;

  rc = sem_post(&sem);
  assert(rc != 0);
  assert(errno == EOVERFLOW);

  rc = sem_getvalue(&sem, &v);
  assert(rc == 0);
  assert(v == SEM_VALUE_MAX);

  rc = sem_destroy(&sem);
  assert(rc == 0);

  return 0;
}
