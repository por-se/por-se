// RUN: %clang %s -emit-llvm -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --posix-runtime --output-dir=%t.klee-out --exit-on-error %t.bc

#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t initialized_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* thread_routine(void* arg) {
  pthread_mutex_lock(&initialized_mutex);
  puts("thread 2");
  pthread_mutex_unlock(&initialized_mutex);
  return NULL;
}

int main(int argc, char **argv) {
  int c = klee_range(0, 256, "range");

  pthread_t thread;
  pthread_create(&thread, NULL, thread_routine, NULL);

  pthread_mutex_lock(&initialized_mutex);
  puts("thread 1: before switch");
  pthread_mutex_unlock(&initialized_mutex);

  switch(c) {
  case 0: puts("0"); break;
  case 10: puts("10"); break;
  case 16: puts("16"); break;
  case 17: puts("17"); break;
  case 18: puts("18"); break;
  case 19: puts("19"); break;
  default: 
    puts("default");
    break;
  }

  pthread_mutex_lock(&initialized_mutex);
  puts("thread 1: after switch");
  pthread_mutex_unlock(&initialized_mutex);

  pthread_join(thread, NULL);

  return 0;
}
