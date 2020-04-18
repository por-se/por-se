// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error --explore-schedules=0 --max-csd-unlimited --thread-scheduling=last %t.bc 2>&1 | FileCheck %s

#include <assert.h>
#include <pthread.h>
#include <stdio.h>

pthread_mutex_t mutex;
pthread_cond_t cond;

static int done = 0;

void* func(void* arg) {
  pthread_mutex_lock(&mutex);

  pthread_cond_wait(&cond, &mutex);

  // CHECK: Woken up!
  // CHECK: Woken up!
  // CHECK: Woken up!
  // CHECK: Woken up!
  // CHECK: Woken up!
  puts("Woken up!");

  done++;
  pthread_mutex_unlock(&mutex);

  return NULL;
}

int main(int argc, char** argv) {
  pthread_t threads[5];

  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);

  for(int t = 0; t < 5; t++) {
    pthread_create(&threads[t], NULL, func, NULL);
  }

  int ready = 0;
  while(!ready) {
    pthread_mutex_lock(&mutex);
    if(done < 5) {
      pthread_cond_broadcast(&cond);
    } else {
      ready = 1;
    }
    pthread_mutex_unlock(&mutex);
  }

  for(int t = 0; t < 5; t++) {
    pthread_join(threads[t], NULL);
  }

  // CHECK: KLEE: done: completed paths = 1
  return 0;
}
