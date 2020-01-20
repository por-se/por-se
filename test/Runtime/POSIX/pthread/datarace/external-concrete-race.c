// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t-first.klee-out
// RUN: rm -rf %t-last.klee-out
// RUN: rm -rf %t-random.klee-out
// RUN: rm -rf %t-round-robin.klee-out
// RUN: %klee -posix-runtime -output-dir=%t-first.klee-out -thread-scheduling=first %t.bc 2>&1 | FileCheck %s
// RUN: %klee -posix-runtime -output-dir=%t-last.klee-out -thread-scheduling=last %t.bc 2>&1 | FileCheck %s
// RUN: %klee -posix-runtime -output-dir=%t-random.klee-out -thread-scheduling=random %t.bc 2>&1 | FileCheck %s
// RUN: %klee -posix-runtime -output-dir=%t-round-robin.klee-out -thread-scheduling=round-robin %t.bc 2>&1 | FileCheck %s

#define _GNU_SOURCE

#include <string.h>
#include <pthread.h>
#include <assert.h>

static char data[] = { 0, 0, 0, 0 };

static void* test(void* arg) {
  memfrob(data, 4);
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_create(&t1, NULL, test, NULL);
  pthread_create(&t2, NULL, test, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  // CHECK: calling external: memfrob
  // CHECK: thread unsafe memory access

  return 0;
}
