#ifndef KLEE_PTHREAD_IMPL_H
#define KLEE_PTHREAD_IMPL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "utils.h"

#ifdef __APPLE__
typedef void* pthread_barrierattr_t;
typedef void* pthread_barrier_t;
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

typedef void* pthread_spinlock_t;
#endif /* __APPLE_ */

typedef struct {
  int acquired;
  int type;
  uint64_t holdingThread;
  kpr_list waitingThreads;
} kpr_mutex;

typedef struct {
  unsigned count;
  unsigned currentCount;
  kpr_list waitingThreads;
} kpr_barrier;

typedef struct {
  uint64_t acquiredWriter;

  kpr_list acquiredReaderLocks;

  size_t waitingWriterCount;
  size_t waitingReaderCount;

  kpr_list waitingList;
} kpr_rwlock;

typedef struct {
  kpr_list waitingList;
  pthread_mutex_t* waitingMutex;
} kpr_cond;

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

  kpr_list cleanUpStack;
} kpr_pthread;

typedef struct {
  int value;
  kpr_list waiting;
  const char* name;
} kpr_semaphore;

//typedef struct {
//  void (*destructor)(void*);
//  void* value;
//} kpr_key;

int kpr_mutex_unlock_internal(pthread_mutex_t *m);

#endif //KLEE_PTHREAD_IMPL_H
