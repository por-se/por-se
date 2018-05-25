#ifndef KLEE_PTHREAD_IMPL_H
#define KLEE_PTHREAD_IMPL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct __pthread_impl_stack_node {
  struct __pthread_impl_stack_node* prev;
  void* data;
} __pthread_impl_stack_node;

typedef struct {
  __pthread_impl_stack_node* top;
  size_t size;
} __pthread_impl_stack;

void __stack_create(__pthread_impl_stack* stack);
void __stack_push(__pthread_impl_stack* stack, void * data);
void* __stack_pop(__pthread_impl_stack* stack);
size_t __stack_size(__pthread_impl_stack* stack);

typedef struct {
  int acquired;
  pthread_t holdingThread;
  __pthread_impl_stack waitingThreads;
} __pthread_impl_mutex;

typedef struct {
  uint64_t tid;
  u_int8_t state;

  void* startArg;
  void* (*startRoutine) (void* arg);

  void* returnValue;
  __pthread_impl_stack waitingForJoinThreads;
} __pthread_impl_pthread;


#endif //KLEE_PTHREAD_IMPL_H
