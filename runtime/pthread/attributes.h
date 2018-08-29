#ifndef KLEE_PTHREAD_ATTR_H
#define KLEE_PTHREAD_ATTR_H

#include <pthread.h>
#include <sys/mman.h>

typedef struct {
  int scope;
  int detachstate;
  void* stackaddr;
  size_t stacksize;
  size_t guardsize;
  int inheritsched;
  int schedpolicy;
} pthread_attr_thread;

typedef struct {
  int type;
  int robust;
  int prioceiling;
  int protocol_;
  int pshared;
} pthread_attr_mutex;

typedef struct {
  int pshared;
  clockid_t clockid;
} pthread_attr_cond;

typedef struct {
  int pshared;
} pthread_attr_barrier;

typedef struct {
  int pshared;
} pthread_attr_rwlock;

#endif //KLEE_PTHREAD_ATTR_H
