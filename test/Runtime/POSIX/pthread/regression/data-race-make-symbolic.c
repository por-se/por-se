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

#include <klee/klee.h>

static char array[4];

static void* test1(void* arg) {
  klee_make_symbolic(array, 4, "array");
  return NULL;
}

static void* test2(void* arg) {
  array[0] = 42;
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_create(&t1, NULL, test1, NULL);
  pthread_create(&t2, NULL, test2, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  // CHECK: thread unsafe memory access

  return 0;
}
