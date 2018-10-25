// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error %t2.bc

/* This is based on the example given at https://www.ibm.com/support/knowledgecenter/en/ssw_i5_54/apis/users_86.htm#372485 */

#include <pthread.h>
#include <stdio.h>

static void checkReturnCode(const char *msg, int rc) {
  if (rc == 0) {
    return;
  }

  printf("ERR: %s failed.\n", msg);
  exit(1);
}

pthread_rwlock_t rwlock;

void *readThread(void *arg) {
  int rc;

  rc = pthread_rwlock_rdlock(&rwlock);
  checkReturnCode("pthread_rwlock_rdlock()\n", rc);
  
  rc = pthread_rwlock_unlock(&rwlock);
  checkReturnCode("pthread_rwlock_unlock()\n", rc);
  
  return NULL;
}

void *writeThread(void *arg) {
  int rc;
  
  rc = pthread_rwlock_wrlock(&rwlock);
  checkReturnCode("pthread_rwlock_wrlock()\n", rc);
  
  rc = pthread_rwlock_unlock(&rwlock);
  checkReturnCode("pthread_rwlock_unlock()\n", rc);
  
  return NULL;
}

int main(int argc, char **argv) {
  int rc = 0;
  pthread_t rdThread;
  pthread_t wrThread;

  rc = pthread_rwlock_init(&rwlock, NULL);
  checkReturnCode("pthread_rwlock_init()\n", rc);

  rc = pthread_rwlock_rdlock(&rwlock);
  checkReturnCode("pthread_rwlock_rdlock()\n",rc);

  rc = pthread_rwlock_rdlock(&rwlock);
  checkReturnCode("pthread_rwlock_rdlock() second\n", rc);

  rc = pthread_create(&rdThread, NULL, readThread, NULL);
  checkReturnCode("pthread_create\n", rc);

  rc = pthread_rwlock_unlock(&rwlock);
  checkReturnCode("pthread_rwlock_unlock()\n", rc);


  rc = pthread_create(&wrThread, NULL, writeThread, NULL);
  checkReturnCode("pthread_create\n", rc);
  
  printf("Main - unlock the second read lock\n");
  rc = pthread_rwlock_unlock(&rwlock);
  checkReturnCode("pthread_rwlock_unlock()\n", rc);


  rc = pthread_join(rdThread, NULL);
  checkReturnCode("pthread_join\n", rc);

  rc = pthread_join(wrThread, NULL);
  checkReturnCode("pthread_join\n", rc);

  rc = pthread_rwlock_destroy(&rwlock);
  checkReturnCode("pthread_rwlock_destroy()\n", rc);

  return 0;
}
