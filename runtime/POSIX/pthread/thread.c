#include "klee/klee.h"
#include "klee/runtime/pthread.h"
#include "klee/runtime/kpr/list.h"

#include <errno.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include "kpr/flags.h"
#include "kpr/internal.h"

static kpr_thread mainThread = {
  .state = KPR_THREAD_STATE_LIVE,
  .mode = KPR_THREAD_MODE_DETACH,

  .startArg = NULL,
  .startRoutine = NULL,

  .returnValue = NULL,

  .cleanupStack = KPR_LIST_INITIALIZER,

  .cond = NULL
};

static _Thread_local kpr_thread* ownThread = NULL;

pthread_t pthread_self(void) {
  void* d = ownThread;

  if (d == NULL) {
    // Main thread will not have any start argument so make sure that we pass the correct one
    return &mainThread;
  }

  return (pthread_t) d;
}

int pthread_equal(pthread_t th1, pthread_t th2) {
  return th1 == th2;
}

static void kpr_wrapper(void* arg) {
  ownThread = (kpr_thread*) arg;

  void* ret = ownThread->startRoutine(ownThread->startArg);
  pthread_exit(ret);
}

int pthread_create(pthread_t *th, const pthread_attr_t *attr, void *(*routine)(void*), void *arg) {
  kpr_thread* thread = calloc(sizeof(kpr_thread), 1);
  *th = thread;

  thread->startRoutine = routine;
  thread->startArg = arg;
  thread->returnValue = NULL;

  thread->state = KPR_THREAD_STATE_LIVE;
  thread->mode = KPR_THREAD_MODE_JOIN;

  if (attr != NULL) {
    int ds = 0;
    pthread_attr_getdetachstate(attr, &ds);

    if (ds == PTHREAD_CREATE_DETACHED) {
      thread->mode = KPR_THREAD_MODE_DETACH;
    }
  }

  kpr_list_create(&thread->cleanupStack);

  klee_create_thread(kpr_wrapper, thread);

  return 0;
}

int pthread_detach(pthread_t pthread) {
  klee_lock_acquire(&pthread->joinLock);
  kpr_thread* thread = pthread;

  if (thread->mode == KPR_THREAD_MODE_DETACH) {
    klee_lock_release(&pthread->joinLock);
    return EINVAL;
  }

  if (thread->mode == KPR_THREAD_MODE_JOINED) {
    klee_lock_release(&pthread->joinLock);
    return EINVAL;
  }

  // One case that we do not have to check is
  // KPR_THREAD_MODE_WAIT_FOR_JOIN
  // -> basically this only happens if the thread
  //    already exited before the detach call
  //    actually happened

  thread->mode = KPR_THREAD_MODE_DETACH;

  klee_lock_release(&pthread->joinLock);

  return 0;
}

void pthread_exit(void* arg) {
  kpr_thread* thread = pthread_self();

  klee_lock_acquire(&thread->joinLock);

  assert(thread->state == KPR_THREAD_STATE_LIVE && "Thread cannot have called exit twice");

  if (thread->mode != KPR_THREAD_MODE_DETACH) {
    thread->returnValue = arg;

    if (thread->mode == KPR_THREAD_MODE_JOINED) {
      // Another thread has joined with us, but is still waiting
      // for the result, as we now have registered the result, we
      // can wake the waiting thread up
      klee_cond_signal(&thread->joinCond);
    }

    if (thread->mode == KPR_THREAD_MODE_JOIN) {
      thread->mode = KPR_THREAD_MODE_WAIT_FOR_JOIN;
    }
  }

  thread->state = KPR_THREAD_STATE_EXITED;

  while (kpr_list_size(&thread->cleanupStack) > 0) {
    pthread_cleanup_pop(1);
  }

  kpr_key_clear_data_of_thread();

  // Must happen together (!)
  klee_lock_release(&thread->joinLock);
  klee_exit_thread();
}

int pthread_join(pthread_t pthread, void **ret) {
  kpr_thread* thread = pthread;

  klee_lock_acquire(&thread->joinLock);

  if (thread->mode == KPR_THREAD_MODE_DETACH) { // detached state
    klee_lock_release(&thread->joinLock);
    return EINVAL;
  }

  if (pthread_self() == pthread) { // We refer to our own thread
    klee_lock_release(&thread->joinLock);
    return EDEADLK;
  }

  if (thread->mode == KPR_THREAD_MODE_JOINED) {
    klee_report_error(__FILE__, __LINE__, "Multiple calls to pthread_join to the same target are undefined", "undef");
  }

  if (thread->mode == KPR_THREAD_MODE_JOIN) {
    thread->mode = KPR_THREAD_MODE_JOINED;

    klee_cond_wait(&thread->joinCond, &thread->joinLock);

    // The thread should now be exited
    assert(thread->state == KPR_THREAD_STATE_EXITED);
  } else if (thread->mode == KPR_THREAD_MODE_WAIT_FOR_JOIN) {
    thread->mode = KPR_THREAD_MODE_JOINED;
  }

  klee_por_thread_join(thread);

  if (ret != NULL) {
    // if (thread->cancelSignalReceived == 1) {
    //  *ret = PTHREAD_CANCELED;
    // } else {
      // If we have returned, then we should be able to return the data
      *ret = thread->returnValue;
    // }
  }

  klee_lock_release(&thread->joinLock);

  return 0;
}

void pthread_cleanup_pop(int execute) {
  pthread_t thread = pthread_self();

  assert(kpr_list_size(&thread->cleanupStack) > 0);
  kpr_cleanup_data* data = (kpr_cleanup_data*) kpr_list_pop(&thread->cleanupStack);

  if (execute != 0) {
    data->routine(data->argument);
  }

  free(data);
}

void pthread_cleanup_push(void (*routine)(void*), void *arg) {
  pthread_t thread = pthread_self();

  kpr_cleanup_data* data = calloc(sizeof(kpr_cleanup_data), 1);
  data->routine = routine;
  data->argument = arg;

  kpr_list_push(&thread->cleanupStack, data);
}

int kpr_signal_thread(pthread_t th) {
  klee_cond_signal(&th->cond);
  return 0;
}

int kpr_wait_thread_self(klee_sync_primitive_t* lock) {
  klee_cond_wait(&pthread_self()->cond, lock);
  return 0;
}
