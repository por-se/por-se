// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -DTDIR=%T -o %t2.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --schedule-forks=sync-point --exit-on-error %t2.bc

#include <pthread.h>
#include <assert.h>

int done = 0;

pthread_mutex_t mutex;
pthread_cond_t cond;

void* func(void* arg) {
  pthread_mutex_lock(&mutex);

  done++;

  pthread_cond_signal(&cond);

  pthread_mutex_unlock(&mutex);

  return NULL;
}

int main(int argc, char** argv) {
  pthread_t threads[2];

  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);

  pthread_mutex_lock(&mutex);

  for(int t = 0; t < 2; t++) {
    pthread_create(&threads[t], NULL, func, NULL);
  }

  while(done < 2) {
    pthread_cond_wait(&cond, &mutex);
  }

  pthread_mutex_unlock(&mutex);

  for(int t = 0; t < 2; t++) {
    pthread_join(threads[t], NULL);
  }

  return 0;
}