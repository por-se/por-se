// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc

#include <pthread.h>
#include <assert.h>

int number = 0;
pthread_t otherThread;

static void triggeredFirst(void* arg) {
  assert(otherThread == pthread_self());
  assert(arg == NULL);

  assert(number == 0);
  number++;
}

static void notTriggered(void* arg) {
  assert(0);
}

static void triggeredAtExit(void* arg) {
  assert(otherThread == pthread_self());
  assert(arg == &number);

  assert(number == 1);
}

static void* thread(void* arg) {
  pthread_cleanup_push(triggeredAtExit, &number);
  pthread_cleanup_push(notTriggered, NULL);
  pthread_cleanup_push(triggeredFirst, NULL);

  pthread_cleanup_pop(1);
  pthread_cleanup_pop(0);

  return NULL;
}

int main(void) {
  pthread_create(&otherThread, NULL, thread, NULL);

  pthread_join(otherThread, NULL);
  return 0;
}
