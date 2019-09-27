// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t-first.klee-out
// RUN: rm -rf %t-last.klee-out
// RUN: rm -rf %t-random.klee-out
// RUN: rm -rf %t-round-robin.klee-out
// RUN: %klee -pthread-runtime -output-dir=%t-first.klee-out -thread-scheduling=first %t.bc 2>&1 | FileCheck %s
// RUN: %klee -pthread-runtime -output-dir=%t-last.klee-out -thread-scheduling=last %t.bc 2>&1 | FileCheck %s
// RUN: %klee -pthread-runtime -output-dir=%t-random.klee-out -thread-scheduling=random %t.bc 2>&1 | FileCheck %s
// RUN: %klee -pthread-runtime -output-dir=%t-round-robin.klee-out -thread-scheduling=round-robin %t.bc 2>&1 | FileCheck %s

#include <pthread.h>
#include <semaphore.h>
#include <assert.h>

static sem_t sem;

static void* test(void* arg) {
  sem_post(&sem);
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t th;

  pthread_create(&th, NULL, test, NULL);
  sem_init(&sem, 0, 0);

  pthread_join(th, NULL);

  // CHECK: {{\(location information missing\)|pthread\/[a-zA-z\/]+.c:[0-9]+:}} thread unsafe memory access

  return 0;
}
