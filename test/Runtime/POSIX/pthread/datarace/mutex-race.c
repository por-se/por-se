// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t-first.klee-out
// RUN: rm -rf %t-last.klee-out
// RUN: rm -rf %t-random.klee-out
// RUN: rm -rf %t-round-robin.klee-out
// RUN: %klee -posix-runtime -output-dir=%t-first.klee-out -thread-scheduling=first %t.bc 2>&1 | FileCheck %s
// RUN: %klee -posix-runtime -output-dir=%t-last.klee-out -thread-scheduling=last %t.bc 2>&1 | FileCheck %s
// RUN: %klee -posix-runtime -output-dir=%t-random.klee-out -thread-scheduling=random %t.bc 2>&1 | FileCheck %s
// RUN: %klee -posix-runtime -output-dir=%t-round-robin.klee-out -thread-scheduling=round-robin %t.bc 2>&1 | FileCheck %s

#include <pthread.h>
#include <assert.h>

static pthread_mutex_t mutex;

static void* test(void* arg) {
  pthread_mutex_lock(&mutex);
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t th;

  pthread_create(&th, NULL, test, NULL);
  pthread_mutex_init(&mutex, NULL);

  pthread_join(th, NULL);

  // CHECK: {{\(location information missing\)|pthread\/[a-zA-z\/]+.c:[0-9]+:}} thread unsafe memory access

  return 0;
}
