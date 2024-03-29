// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

static void* test(void* arg) {
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t t1, t2;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&t1, &attr, test, NULL);
  pthread_create(&t2, &attr, test, NULL);

  // If the threads will not exit, then klee will report and error
  pthread_attr_destroy(&attr);

  return 0;
}
