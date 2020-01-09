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

static volatile int num[3];

static void* test(void* arg) {
  int* refIndex = (int*) arg;
  num[*refIndex] = num[*refIndex] + 1;
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  int index1 = klee_int("index1");
  int index2 = klee_int("index2");
  klee_assume(index1 >= 0 & index1 <= 1);
  klee_assume(index2 >= 1 & index2 <= 2);

  pthread_create(&t1, NULL, test, &index1);
  pthread_create(&t2, NULL, test, &index2);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(num[0] + num[1] + num[2] == 2 && "all paths without data race perform exactly one increment per thread");

  // CHECK: thread unsafe memory access

  return 0;
}
