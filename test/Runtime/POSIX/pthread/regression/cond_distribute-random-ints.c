// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc | FileCheck %s

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define NUM_THREADS 1

static size_t randomints[NUM_THREADS];

static size_t initialized;
static pthread_cond_t initialized_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initialized_mutex = PTHREAD_MUTEX_INITIALIZER;

void *thread_routine(void *arg) {
  // initialize
  pthread_mutex_lock(&initialized_mutex);
  const size_t id = (size_t) arg;
  const size_t number = randomints[id]; // save number (no used in this simple version)
  if (++initialized == NUM_THREADS) {
    pthread_cond_signal(&initialized_cond);
    puts("last init finished, signalling main");
  }
  pthread_mutex_unlock(&initialized_mutex);

  pthread_exit(0);
}

int main(int argc, char *argv[]) {
  // draw random numbers
  for (int i = 0; i < NUM_THREADS; ++i) {
    bool unique;
    size_t draw;
    do {
      unique = true;
      draw = rand() % NUM_THREADS;
      for (int j = 0; j < i; ++j) {
        if (randomints[j] != draw) continue;
        unique = false;
        break;
      }
    }
    while(!unique);
    randomints[i] = draw;
  }

  // create threads
  pthread_t threads[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; ++i) {
    pthread_create(&threads[i], NULL, thread_routine, (void *)(size_t)i);
  }

  // wait for initialization of threads
  pthread_mutex_lock(&initialized_mutex);
  // CHECK-NOT: thread unsafe memory access
  while (initialized != NUM_THREADS) {
    pthread_cond_wait(&initialized_cond, &initialized_mutex);
    puts("[main] woke up");
  }
  pthread_mutex_unlock(&initialized_mutex);
  // CHECK: initialization finished!
  puts("initialization finished!");

  for (int i = 0; i < NUM_THREADS; ++i) {
    pthread_join(threads[i], NULL);
  }

  return 0;
}
