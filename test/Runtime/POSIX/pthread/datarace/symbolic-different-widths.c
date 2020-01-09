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

static int num = 0;

static int index1;
static int index2;

static void* test1(void* arg) {
  short* array = (short*) &num;

  array[index1]++;

  return NULL;
}

static void* test2(void* arg) {
  char* array = (char*) &num;

  // The original object has 4 bytes of values, so we either
  // write to one of the 'middle' bytes
  array[1 + index2]++;

  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  index1 = klee_int("index1");
  index2 = klee_int("index2");
  klee_assume(index1 >= 0 & index1 <= 1);
  klee_assume(index2 >= 0 & index2 <= 1);

  pthread_create(&t1, NULL, test1, NULL);
  pthread_create(&t2, NULL, test2, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  // CHECK: thread unsafe memory access

  return 0;
}
