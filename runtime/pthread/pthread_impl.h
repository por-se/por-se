#ifndef KLEE_PTHREAD_IMPL_H
#define KLEE_PTHREAD_IMPL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __APPLE__
typedef void* pthread_barrierattr_t;
typedef void* pthread_barrier_t;
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

typedef void* pthread_spinlock_t;
#endif /* __APPLE_ */

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
  int type;
  pthread_t holdingThread;
  __pthread_impl_stack waitingThreads;
} __pthread_impl_mutex;

typedef struct {
  int type;
} __pthread_impl_mutex_attr;

typedef struct {
  unsigned count;
  unsigned currentCount;
  __pthread_impl_stack waitingThreads;
} __pthread_impl_barrier;

typedef struct {
  uint8_t mode;
  uint64_t acquiredWriter;
  uint64_t acquiredReaderCount;

  __pthread_impl_stack waitingWriters;
  __pthread_impl_stack waitingReaders;
} __pthread_impl_rwlock;

typedef struct {
  uint8_t mode;

  __pthread_impl_stack waitingList;
} __pthread_impl_cond;

typedef struct {
  uint64_t tid;
  uint8_t state;
  uint8_t mode;
  uint8_t cancelSignalReceived;
  int cancelState;

  void* startArg;
  void* (*startRoutine) (void* arg);

  void* returnValue;
  uint8_t joinState;
  uint64_t joinedThread;
} __pthread_impl_pthread;

typedef struct {
  int value;
  __pthread_impl_stack waiting;
} __pthread_impl_semaphore;

typedef struct {
  uint8_t called;
} __pthread_impl_once;

int __pthread_mutex_unlock_internal(pthread_mutex_t *m);
void __notify_threads(__pthread_impl_stack* stack);

#endif //KLEE_PTHREAD_IMPL_H
