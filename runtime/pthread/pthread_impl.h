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
  __kpr_list waitingThreads;
} __kpr_mutex;

typedef struct {
  unsigned count;
  unsigned currentCount;
  __kpr_list waitingThreads;
} __kpr_barrier;

typedef struct {
  uint64_t acquiredWriter;

  __kpr_list acquiredReaderLocks;

  size_t waitingWriterCount;
  size_t waitingReaderCount;

  __kpr_list waitingList;
} __kpr_rwlock;

typedef struct {
  uint8_t mode;

  __kpr_list waitingList;
} __kpr_cond;

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

  __kpr_list cleanUpStack;
} __kpr_pthread;

typedef struct {
  int value;
  __kpr_list waiting;
  const char* name;
} __kpr_semaphore;

typedef struct {
  void (*destructor)(void*);
  void* value;
} __kpr_key;

int __pthread_mutex_unlock_internal(pthread_mutex_t *m);

#endif //KLEE_PTHREAD_IMPL_H
