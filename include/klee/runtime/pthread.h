#ifndef _PTHREAD_H
#define _PTHREAD_H

#if defined(_PORSE_PURE_HEADER)
#warning "Using pure PORSE runtime headers"
#endif

#if !defined(_PORSE_PURE_HEADER)
#include <sched.h>
#include <time.h>
#endif

#include "kpr/list-types.h"
#include "klee_types.h"

#define _USING_PORSE_PTHREAD (1)

// What is the magic stuff?
// The magic stuff is a pattern that is detected in the runtime to distinguish between
// correctly initialized static mutexes and zero initialized ones
typedef struct {
  // Has to be a volatile so that the data race detection can properly detect
  // races between initialization and other events
  volatile char magic;
} pthread_internal_t;
#define PTHREAD_INTERNAL_MAGIC_VALUE 42
#define PTHREAD_INTERNAL_MAGIC {.magic = 42}

// Constants that should be defined
#define PTHREAD_BARRIER_SERIAL_THREAD -1

/* Cancellation */
enum
{
  PTHREAD_CANCEL_ENABLE,
#define PTHREAD_CANCEL_ENABLE   PTHREAD_CANCEL_ENABLE
  PTHREAD_CANCEL_DISABLE
#define PTHREAD_CANCEL_DISABLE  PTHREAD_CANCEL_DISABLE
};
enum
{
  PTHREAD_CANCEL_DEFERRED,
#define PTHREAD_CANCEL_DEFERRED	PTHREAD_CANCEL_DEFERRED
  PTHREAD_CANCEL_ASYNCHRONOUS
#define PTHREAD_CANCEL_ASYNCHRONOUS	PTHREAD_CANCEL_ASYNCHRONOUS
};
#define PTHREAD_CANCELED ((void *) -1)

enum
{
  PTHREAD_CREATE_JOINABLE,
#define PTHREAD_CREATE_JOINABLE	PTHREAD_CREATE_JOINABLE
  PTHREAD_CREATE_DETACHED
#define PTHREAD_CREATE_DETACHED	PTHREAD_CREATE_DETACHED
};

/* Scheduler inheritance.  */
enum
{
  PTHREAD_INHERIT_SCHED,
#define PTHREAD_INHERIT_SCHED   PTHREAD_INHERIT_SCHED
  PTHREAD_EXPLICIT_SCHED
#define PTHREAD_EXPLICIT_SCHED  PTHREAD_EXPLICIT_SCHED
};

/* Mutex types.  */
enum
{
  PTHREAD_MUTEX_NORMAL,
  PTHREAD_MUTEX_RECURSIVE,
  PTHREAD_MUTEX_ERRORCHECK,
  PTHREAD_MUTEX_DEFAULT = PTHREAD_MUTEX_NORMAL
};

/* Robust mutex or not flags.  */
enum
{
  PTHREAD_MUTEX_STALLED,
  PTHREAD_MUTEX_ROBUST,
};

/* Mutex protocols.  */
enum
{
  PTHREAD_PRIO_NONE,
  PTHREAD_PRIO_INHERIT,
  PTHREAD_PRIO_PROTECT
};

/* Special kpr addition  */
enum
{
  // Never set by ourself
  KPR_TRYLOCK_UNKNOWN,

  KPR_TRYLOCK_ENABLED,
  KPR_TRYLOCK_DISABLED
};

/* Process shared or private flag.  */
enum
{
  PTHREAD_PROCESS_PRIVATE,
#define PTHREAD_PROCESS_PRIVATE PTHREAD_PROCESS_PRIVATE
  PTHREAD_PROCESS_SHARED
#define PTHREAD_PROCESS_SHARED  PTHREAD_PROCESS_SHARED
};

/* Scope handling.  */
enum
{
  PTHREAD_SCOPE_SYSTEM,
#define PTHREAD_SCOPE_SYSTEM    PTHREAD_SCOPE_SYSTEM
  PTHREAD_SCOPE_PROCESS
#define PTHREAD_SCOPE_PROCESS   PTHREAD_SCOPE_PROCESS
};

typedef struct kpr_cond {
  pthread_internal_t magic;

  klee_sync_primitive_t internalCond;
  klee_sync_primitive_t lock;

  struct kpr_mutex* waitingMutex;
  unsigned long waitingCount;
} pthread_cond_t;
#define PTHREAD_COND_INITIALIZER { PTHREAD_INTERNAL_MAGIC, 0, 0, NULL, 0 }

struct kpr_thread_data {
  int detached;

  // cond variable that is only used to wait until a thread exits.
  // Note: only available if the thread is not detached
  klee_sync_primitive_t joinCond;

  // cond variable that only this thread uses to put itself into a waiting
  // state. Other threads have to signal this thread by using this cond
  // variable.
  klee_sync_primitive_t selfWaitCond;

  void* startArg;
  void* returnValue;

  void* (*threadFunction) (void* arg);

  kpr_list cleanupStack;
};

struct kpr_thread {
  // This `kpr_thread` structure is available for every thread once the
  // thread is created. If a thread exists, then the `kpr_thread` is still
  // available to determine the state of the thread (e.g. for robust mutexes).
  // In contrast, the `kpr_thread_data` (data field in this structure) is
  // only relevant during a thread's lifetime. It is only available until
  // the thread is cleaned up.

  
  // The current state of the thread: e.g. alive, exited
  int state;

  // Lock to guard this thread structure and the nested `data` structure.
  // Can be used in conjunction with the `data->joinCond`
  klee_sync_primitive_t lock;

  struct kpr_thread_data* data;
};

typedef struct kpr_thread* pthread_t;

typedef struct kpr_mutex {
  pthread_internal_t magic;

  klee_sync_primitive_t lock;
  klee_sync_primitive_t cond;

  int type; // normal, errorcheck, recursive
  int robust; // stalled, robust

  int acquired;
  pthread_t holdingThread;

  int robustState;

  int trylock_support;
} pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER { PTHREAD_INTERNAL_MAGIC, 0, 0, PTHREAD_MUTEX_DEFAULT, PTHREAD_MUTEX_STALLED, 0, NULL, 0, KPR_TRYLOCK_UNKNOWN }
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP { PTHREAD_INTERNAL_MAGIC, 0, 0, PTHREAD_MUTEX_RECURSIVE, PTHREAD_MUTEX_STALLED, 0, NULL, 0, KPR_TRYLOCK_UNKNOWN }

#define KPR_MUTEX_INITIALIZER_TRYLOCK { PTHREAD_INTERNAL_MAGIC, 0, 0, PTHREAD_MUTEX_DEFAULT, PTHREAD_MUTEX_STALLED, 0, NULL, 0, KPR_TRYLOCK_ENABLED }

typedef struct {
  pthread_internal_t magic;
  pthread_t acquiredWriter;
  unsigned long acquiredReaderCount;

  pthread_mutex_t mutex;
  pthread_cond_t cond;
} pthread_rwlock_t;
#define PTHREAD_RWLOCK_INITIALIZER { PTHREAD_INTERNAL_MAGIC, NULL, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER }

typedef pthread_mutex_t pthread_spinlock_t;

typedef struct {
  int called;
  pthread_mutex_t mutex;
} pthread_once_t;
#define PTHREAD_ONCE_INIT { 0, PTHREAD_MUTEX_INITIALIZER }

// Primitives that need other primitives

typedef struct {
  pthread_internal_t magic;

  unsigned count;
  unsigned currentCount;

  pthread_mutex_t mutex;
  pthread_cond_t cond;
} pthread_barrier_t;

// Attributes

typedef struct {
  int detachstate;
} pthread_attr_t;

typedef struct {
  int pshared;
} pthread_barrierattr_t;

typedef struct {
  int pshared;
  clockid_t clock;
} pthread_condattr_t;

typedef struct {
  int type;
  int robust;
  int pshared;

  int trylock_support;
} pthread_mutexattr_t;

typedef struct {
  int pshared;
} pthread_rwlockattr_t;

typedef struct {
  int index;
  int generation;
} kpr_key;

typedef kpr_key* pthread_key_t;

#define PTHREAD_DESTRUCTOR_ITERATIONS (16)
#define PTHREAD_KEYS_MAX (256)

// Main threading api
int pthread_create(pthread_t *th, const pthread_attr_t *attr, void *(*routine)(void*), void *arg);
int pthread_detach(pthread_t th);
int pthread_equal(pthread_t th1, pthread_t th2);
void pthread_exit(void *ret) __attribute__((noreturn));
int pthread_join(pthread_t th, void **ret);
pthread_t pthread_self(void);

// Barrier
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned count);
int pthread_barrier_wait(pthread_barrier_t *barrier);

// Condition variables
int pthread_cond_broadcast(pthread_cond_t * cond);
int pthread_cond_destroy(pthread_cond_t * cond);
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_signal(pthread_cond_t * cond);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *time);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);

// Mutex locks
int pthread_mutex_consistent(pthread_mutex_t *lock);
int pthread_mutex_destroy(pthread_mutex_t *lock);
int pthread_mutex_getprioceiling(const pthread_mutex_t *lock, int *prioceiling);
int pthread_mutex_init(pthread_mutex_t *lock, const pthread_mutexattr_t *attr);
int pthread_mutex_lock(pthread_mutex_t *lock);
int pthread_mutex_setprioceiling(pthread_mutex_t *lock, int p1, int *p2);
int pthread_mutex_timedlock(pthread_mutex_t *lock, const struct timespec *time);
int pthread_mutex_trylock(pthread_mutex_t *lock);
int pthread_mutex_unlock(pthread_mutex_t *lock);

// Read write locks
int pthread_rwlock_destroy(pthread_rwlock_t *lock);
int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr);
int pthread_rwlock_rdlock(pthread_rwlock_t *lock);
int pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *time);
int pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *time);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *lock);
int pthread_rwlock_unlock(pthread_rwlock_t *lock);
int pthread_rwlock_wrlock(pthread_rwlock_t *lock);

// Spin locks
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

// thread cancellation
int pthread_cancel(pthread_t th);
int pthread_setcancelstate(int p1, int *p2);
int pthread_setcanceltype(int p1, int *p2);
void pthread_testcancel(void);

// thread keys
void *pthread_getspecific(pthread_key_t key);
int pthread_key_create(pthread_key_t *key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void * v);

// General multithreading
int pthread_atfork(void (*prepare)(void), void (*parent)(void), void(*child)(void));
int pthread_once(pthread_once_t *once, void (*oncefunc)(void));
// More scheduling that is not supported
//int pthread_getconcurrency(void);
//int pthread_getcpuclockid(pthread_t th, clockid_t *clock);
//int pthread_getschedparam(pthread_t th, int *c, struct sched_param *schedparam);
//int pthread_setconcurrency(int concurrency);
//int pthread_setschedparam(pthread_t, int c, const struct sched_param *schedparam);
//int pthread_setschedprio(pthread_t, int schedprio);

// Thread attributes
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_getdetachstate(const pthread_attr_t * attr, int * detachstate);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);

// These are not supported since the stack is located inside klee
//int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *guardsize);
//int pthread_attr_getstack(const pthread_attr_t *attr, void **restrict, size_t *stack);
//int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
//int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize);
//int pthread_attr_setstack(pthread_attr_t *attr, void *, size_t stack);
//int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size);

// Also not supported for now as they rely on scheduling differences
//int pthread_attr_setscope(pthread_attr_t *attr, int scope);
//int pthread_attr_getscope(const pthread_attr_t *attr, int *scope);
//int pthread_attr_getschedparam(const pthread_attr_t *attr, struct sched_param *schedparam);
//int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *schedpolicy);
//int pthread_attr_setschedparam(pthread_attr_t *attr, const struct sched_param * schedparam);
//int pthread_attr_setschedpolicy(pthread_attr_t *attr, int schedpolicy);
//int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched);
//int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inheritsched);

// Barrier attributes
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr);
int pthread_barrierattr_getpshared(const pthread_barrierattr_t *attr, int *pshared);
int pthread_barrierattr_init(pthread_barrierattr_t *attr);
int pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared);

// Condition variable attributes
int pthread_condattr_destroy(pthread_condattr_t *attr);
int pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *clock);
int pthread_condattr_getpshared(const pthread_condattr_t *attr, int *pshared);
int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock);
int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared);

// Mutex attributes
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr, int *pshared);
int pthread_mutexattr_getrobust(const pthread_mutexattr_t *attr, int *robust);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared);
int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int robust);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
// These below are for now not supported as they can have an influence on how the
// mutex can be acquired
//int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *attr, int *prioceiling);
//int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr, int *protocol);
//int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int prioceiling);
//int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol);

// This is a special addition that we only support for the kpr runtime
int kpr_pthread_mutexattr_settrylock(pthread_mutexattr_t *attr, int enabled);
int kpr_pthread_mutexattr_gettrylock(const pthread_mutexattr_t *attr, int* enabled);

// Rwlock attributes
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr, int *pshared);
int pthread_rwlockattr_init(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared);

void pthread_cleanup_pop(int execute);
void pthread_cleanup_push(void (*routine)(void*), void *arg);

#endif // _PTHREAD_H
