// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t-first.klee-out
// RUN: rm -rf %t-last.klee-out
// RUN: rm -rf %t-random.klee-out
// RUN: rm -rf %t-round-robin.klee-out
// RUN: %klee -pthread-runtime -exit-on-error-type=Assert -output-dir=%t-first.klee-out -thread-scheduling=first %t.bc 2>&1
// RUN: %klee -pthread-runtime -exit-on-error-type=Assert -output-dir=%t-last.klee-out -thread-scheduling=last %t.bc 2>&1
// RUN: %klee -pthread-runtime -exit-on-error-type=Assert -output-dir=%t-random.klee-out -thread-scheduling=random %t.bc 2>&1
// RUN: %klee -pthread-runtime -exit-on-error-type=Assert -output-dir=%t-round-robin.klee-out -thread-scheduling=round-robin %t.bc 2>&1
// RUN: test -f %t-first.klee-out/test000001.unsafememoryaccess.err
// RUN: test -f %t-last.klee-out/test000001.unsafememoryaccess.err
// RUN: test -f %t-random.klee-out/test000001.unsafememoryaccess.err
// RUN: test -f %t-round-robin.klee-out/test000001.unsafememoryaccess.err


#include <pthread.h>
#include <assert.h>

static int num = 0;

static void* test(void* arg) {
  num++;
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_create(&t1, NULL, test, NULL);
  pthread_create(&t2, NULL, test, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(0);

  return 0;
}
