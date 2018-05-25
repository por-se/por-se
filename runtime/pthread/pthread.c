#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define blindCast(type,value) ((type) *((type *) ((void*) &(value))))

void __stack_create(__pthread_impl_stack* stack) {
  stack->size = 0;
  stack->top = NULL;
}

void __stack_push(__pthread_impl_stack* stack, void * data) {
  __pthread_impl_stack_node* newTop = malloc(sizeof(__pthread_impl_stack_node));
  newTop->data = data;
  newTop->prev = stack->top;
  stack->top = newTop;
}

void* __stack_pop(__pthread_impl_stack* stack) {
  __pthread_impl_stack_node* top = stack->top;
  stack->top = top->prev;
  stack->size--;
  void* data = top->data;
  free(top);
  return data;
}

size_t __stack_size(__pthread_impl_stack* stack) {
  return stack->size;
}

static __pthread_impl_pthread* __obtain_pthread_data(pthread_t pthread) {
  return ((__pthread_impl_pthread*)pthread);
}

static __pthread_impl_mutex* __obtain_mutex_data(pthread_mutex_t *mutex) {
  return *((__pthread_impl_mutex**)mutex);
}

static void __notify_threads(__pthread_impl_stack* stack) {
  size_t size = __stack_size(stack);
  uint64_t* tids = malloc(sizeof(uint64_t) * size);

  for (size_t i = 0; i < size; i++) {
    uint64_t data = (uint64_t) __stack_pop(stack);
    tids[i] = data;
  }

  klee_wake_up_threads(tids, size);
}

static void* __pthread_impl_wrapper(void* arg) {
  __pthread_impl_pthread* thread = arg;
  void* ret = thread->startRoutine(thread->startArg);
  pthread_exit(ret);
}

int pthread_create(pthread_t *pthread, const pthread_attr_t *attr, void *(*startRoutine)(void *), void *arg) {
  __pthread_impl_pthread* thread = malloc(sizeof(__pthread_impl_pthread));
  memset(thread, 0, sizeof(__pthread_impl_mutex));

  *((__pthread_impl_pthread**)pthread) = thread;

  uint64_t tid = (uint64_t) thread;
  thread->tid = tid;
  thread->startRoutine = startRoutine;
  thread->startArg = arg;
  thread->returnValue = NULL;
  thread->state = 0;
  __stack_create(&thread->waitingForJoinThreads);

  klee_create_thread(tid, __pthread_impl_wrapper, thread);

  return 0;
}

//int pthread_detach(pthread_t);

void pthread_exit(void* arg) __attribute__ ((__noreturn__)) {
  uint64_t tid = klee_get_thread_id();

  if (tid != 0) { // 0 is the main thread and nobody can join it
    __pthread_impl_pthread* thread = (__pthread_impl_pthread*) tid;
    thread->returnValue = arg;
    __notify_threads(&thread->waitingForJoinThreads);

    thread->state = 1;
  }

  klee_exit_thread();
}

int pthread_join(pthread_t pthread, void **ret) {
  __pthread_impl_pthread* thread = __obtain_pthread_data(pthread);

  if (__stack_size(&thread->waitingForJoinThreads) == 1) {
    return EINVAL;
  }

  // Could also be that this thread is already finished, but there must be
  // at least one call to join to free the resources

  if (thread->state != 1) {
    uint64_t ownThread = klee_get_thread_id();

    // We refer to our one thread
    if (ownThread == pthread) {
      return EDEADLK;
    }

    __stack_push(&thread->waitingForJoinThreads, (void*) ownThread);
    klee_sleep_thread();
  }

  // If we have returned, then we should be able to return the data
  *ret = thread->returnValue;

  return 0;
}

//
//pthread_t pthread_self(void);
//
//int pthread_equal(pthread_t, pthread_t);
//
//#ifndef __cplusplus
//#define pthread_equal(x,y) ((x)==(y))
//#endif
//
//int pthread_setcancelstate(int, int *);
//int pthread_setcanceltype(int, int *);
//void pthread_testcancel(void);
//int pthread_cancel(pthread_t);
//
//int pthread_getschedparam(pthread_t, int *__restrict, struct sched_param *__restrict);
//int pthread_setschedparam(pthread_t, int, const struct sched_param *);
//int pthread_setschedprio(pthread_t, int);
//
//int pthread_once(pthread_once_t *, void (*)(void));
//

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
  __pthread_impl_mutex* mutex = malloc(sizeof(__pthread_impl_mutex));
  memset(mutex, 0, sizeof(__pthread_impl_mutex));

  *((__pthread_impl_mutex**)m) = mutex;

  mutex->acquired = 0;
  mutex->holdingThread = 0;
  __stack_create(&mutex->waitingThreads);

  klee_warning("Lock init not acquired");
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0) {
    mutex->acquired = 1;
    mutex->holdingThread = tid;

    klee_warning("Got a lock - preempting");

    // We have acquired a lock, so make sure that we sync threads
    klee_preempt_thread();
    return 0;
  }

  klee_stack_trace();
  klee_warning("Lock already locked - sleeping");

  __stack_push(&mutex->waitingThreads, (void*) tid);
  klee_sleep_thread();
  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  uint64_t tid = klee_get_thread_id();

  if (mutex->acquired == 0 || mutex->holdingThread != tid) {
    return -1;
  }

  klee_warning("Lock unlocking - waking up threads");
  mutex->acquired = 0;

  __notify_threads(&mutex->waitingThreads);
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
  __pthread_impl_mutex* mutex = __obtain_mutex_data(m);
  if (mutex->acquired == 0) {
    return pthread_mutex_lock(m);
  }

  klee_preempt_thread();
  return EBUSY;
}

//int pthread_mutex_timedlock(pthread_mutex_t *__restrict, const struct timespec *__restrict);
//int pthread_mutex_destroy(pthread_mutex_t *);
//int pthread_mutex_consistent(pthread_mutex_t *);
//
//int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict, int *__restrict);
//int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict, int, int *__restrict);
//
//int pthread_cond_init(pthread_cond_t *__restrict, const pthread_condattr_t *__restrict);
//int pthread_cond_destroy(pthread_cond_t *);
//int pthread_cond_wait(pthread_cond_t *__restrict, pthread_mutex_t *__restrict);
//int pthread_cond_timedwait(pthread_cond_t *__restrict, pthread_mutex_t *__restrict, const struct timespec *__restrict);
//int pthread_cond_broadcast(pthread_cond_t *);
//int pthread_cond_signal(pthread_cond_t *);
//
//int pthread_rwlock_init(pthread_rwlock_t *__restrict, const pthread_rwlockattr_t *__restrict);
//int pthread_rwlock_destroy(pthread_rwlock_t *);
//int pthread_rwlock_rdlock(pthread_rwlock_t *);
//int pthread_rwlock_tryrdlock(pthread_rwlock_t *);
//int pthread_rwlock_timedrdlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);
//int pthread_rwlock_wrlock(pthread_rwlock_t *);
//int pthread_rwlock_trywrlock(pthread_rwlock_t *);
//int pthread_rwlock_timedwrlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);
//int pthread_rwlock_unlock(pthread_rwlock_t *);
//
//int pthread_spin_init(pthread_spinlock_t *, int);
//int pthread_spin_destroy(pthread_spinlock_t *);
//int pthread_spin_lock(pthread_spinlock_t *);
//int pthread_spin_trylock(pthread_spinlock_t *);
//int pthread_spin_unlock(pthread_spinlock_t *);
//
//int pthread_barrier_init(pthread_barrier_t *__restrict, const pthread_barrierattr_t *__restrict, unsigned);
//int pthread_barrier_destroy(pthread_barrier_t *);
//int pthread_barrier_wait(pthread_barrier_t *);
//
//int pthread_key_create(pthread_key_t *, void (*)(void *));
//int pthread_key_delete(pthread_key_t);
//void *pthread_getspecific(pthread_key_t);
//int pthread_setspecific(pthread_key_t, const void *);
//
//int pthread_attr_init(pthread_attr_t *);
//int pthread_attr_destroy(pthread_attr_t *);
//
//int pthread_attr_getguardsize(const pthread_attr_t *__restrict, size_t *__restrict);
//int pthread_attr_setguardsize(pthread_attr_t *, size_t);
//int pthread_attr_getstacksize(const pthread_attr_t *__restrict, size_t *__restrict);
//int pthread_attr_setstacksize(pthread_attr_t *, size_t);
//int pthread_attr_getdetachstate(const pthread_attr_t *, int *);
//int pthread_attr_setdetachstate(pthread_attr_t *, int);
//int pthread_attr_getstack(const pthread_attr_t *__restrict, void **__restrict, size_t *__restrict);
//int pthread_attr_setstack(pthread_attr_t *, void *, size_t);
//int pthread_attr_getscope(const pthread_attr_t *__restrict, int *__restrict);
//int pthread_attr_setscope(pthread_attr_t *, int);
//int pthread_attr_getschedpolicy(const pthread_attr_t *__restrict, int *__restrict);
//int pthread_attr_setschedpolicy(pthread_attr_t *, int);
//int pthread_attr_getschedparam(const pthread_attr_t *__restrict, struct sched_param *__restrict);
//int pthread_attr_setschedparam(pthread_attr_t *__restrict, const struct sched_param *__restrict);
//int pthread_attr_getinheritsched(const pthread_attr_t *__restrict, int *__restrict);
//int pthread_attr_setinheritsched(pthread_attr_t *, int);
//
//int pthread_mutexattr_destroy(pthread_mutexattr_t *);
//int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getpshared(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_gettype(const pthread_mutexattr_t *__restrict, int *__restrict);
//int pthread_mutexattr_init(pthread_mutexattr_t *);
//int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);
//int pthread_mutexattr_setrobust(pthread_mutexattr_t *, int);
//int pthread_mutexattr_settype(pthread_mutexattr_t *, int);
//
//int pthread_condattr_init(pthread_condattr_t *);
//int pthread_condattr_destroy(pthread_condattr_t *);
//int pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
//int pthread_condattr_setpshared(pthread_condattr_t *, int);
//int pthread_condattr_getclock(const pthread_condattr_t *__restrict, clockid_t *__restrict);
//int pthread_condattr_getpshared(const pthread_condattr_t *__restrict, int *__restrict);
//
//int pthread_rwlockattr_init(pthread_rwlockattr_t *);
//int pthread_rwlockattr_destroy(pthread_rwlockattr_t *);
//int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *, int);
//int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *__restrict, int *__restrict);
//
//int pthread_barrierattr_destroy(pthread_barrierattr_t *);
//int pthread_barrierattr_getpshared(const pthread_barrierattr_t *__restrict, int *__restrict);
//int pthread_barrierattr_init(pthread_barrierattr_t *);
//int pthread_barrierattr_setpshared(pthread_barrierattr_t *, int);
//
//int pthread_atfork(void (*)(void), void (*)(void), void (*)(void));
//
//int pthread_getconcurrency(void);
//int pthread_setconcurrency(int);
//
//int pthread_getcpuclockid(pthread_t, clockid_t *);