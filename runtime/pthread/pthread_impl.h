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

#define KPR_THREAD_MODE_JOIN (0)
#define KPR_THREAD_MODE_DETACH (1)

#define KPR_THREAD_JSTATE_JOINABLE (0)
#define KPR_THREAD_JSTATE_WAIT_FOR_JOIN (1)
#define KPR_THREAD_JSTATE_JOINED (2)

#define KPR_THREAD_JOINED (1)
#define KPR_THREAD_WAIT_FOR_JOIN (4)

typedef struct {
  int acquired;
  int type;
  uint64_t holdingThread;
} kpr_mutex;

typedef struct {
  unsigned count;
  unsigned currentCount;
} kpr_barrier;

typedef struct {
  uint64_t acquiredWriter;
  uint64_t acquiredReaderCount;

  size_t waitingWriterCount;
  size_t waitingReaderCount;

} kpr_rwlock;

typedef struct {
  pthread_mutex_t* waitingMutex;
  uint64_t waitingCount;
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

  uint8_t joinDetachState;

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
