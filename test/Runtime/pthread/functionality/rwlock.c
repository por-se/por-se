// RUN: %llvmgcc %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t.bc

/* This is based on the example given at https://www.ibm.com/support/knowledgecenter/en/ssw_i5_54/apis/users_86.htm#372485 */

#include <pthread.h>
#include <assert.h>

pthread_rwlock_t rwlock;

void *readThread(void *arg) {
  int rc;

  rc = pthread_rwlock_rdlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_rdlock() failed");

  rc = pthread_rwlock_unlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_unlock() failed");

  return NULL;
}

void *writeThread(void *arg) {
  int rc;

  rc = pthread_rwlock_wrlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_wrlock() failed");

  rc = pthread_rwlock_unlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_unlock() failed");

  return NULL;
}

int main(int argc, char **argv) {
  int rc = 0;
  pthread_t rdThread;
  pthread_t wrThread;

  rc = pthread_rwlock_init(&rwlock, NULL);
  assert(rc == 0 && "pthread_rwlock_init() failed");

  rc = pthread_rwlock_rdlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_rdlock() failed");

  rc = pthread_rwlock_rdlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_rdlock() second failed");

  rc = pthread_create(&rdThread, NULL, readThread, NULL);
  assert(rc == 0 && "pthread_create() failed");

  rc = pthread_rwlock_unlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_unlock() failed");


  rc = pthread_create(&wrThread, NULL, writeThread, NULL);
  assert(rc == 0 && "pthread_create() failed");

  rc = pthread_rwlock_unlock(&rwlock);
  assert(rc == 0 && "pthread_rwlock_unlock() failed");


  rc = pthread_join(rdThread, NULL);
  assert(rc == 0 && "pthread_join() failed");

  rc = pthread_join(wrThread, NULL);
  assert(rc == 0 && "pthread_join() failed");

  rc = pthread_rwlock_destroy(&rwlock);
  assert(rc == 0 && "pthread_rwlock_destroy() failed");

  return 0;
}
