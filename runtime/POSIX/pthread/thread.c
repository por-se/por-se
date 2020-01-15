#include "klee/klee.h"
#include "klee/runtime/pthread.h"
#include "klee/runtime/kpr/list.h"

#include <errno.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include "kpr/flags.h"
#include "kpr/internal.h"

static struct kpr_thread_data mainThreadData = {
  .detached = true,

  .startArg = NULL,
  .threadFunction = NULL,

  .returnValue = NULL,

  .cleanupStack = KPR_LIST_INITIALIZER
};

static struct kpr_thread mainThread = {
  .state = KPR_THREAD_STATE_LIVE,

  .data = &mainThreadData
};

static _Thread_local struct kpr_thread* ownThread = NULL;

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
  ownThread = (struct kpr_thread*) arg;

  void* ret = ownThread->data->threadFunction(ownThread->data->startArg);
  pthread_exit(ret);
}

int pthread_create(pthread_t *th, const pthread_attr_t *attr, void *(*routine)(void*), void *arg) {
  struct kpr_thread* thread = calloc(sizeof(struct kpr_thread), 1);
  struct kpr_thread_data* thread_data = calloc(sizeof(struct kpr_thread_data), 1);
  *th = thread;

  thread->data = thread_data;

  thread_data->threadFunction = routine;
  thread_data->startArg = arg;
  thread_data->returnValue = NULL;

  thread->state = KPR_THREAD_STATE_LIVE;
  thread_data->detached = false;

  if (attr != NULL) {
    int ds = 0;
    pthread_attr_getdetachstate(attr, &ds);

    if (ds == PTHREAD_CREATE_DETACHED) {
      thread_data->detached = true;
    }
  }

  kpr_list_create(&thread_data->cleanupStack);

  klee_create_thread(kpr_wrapper, thread);

  return 0;
}

int pthread_detach(pthread_t pthread) {
  klee_sync_primitive_t* lock_id = &pthread->lock;

  klee_lock_acquire(lock_id);
  struct kpr_thread* thread = pthread;

  if (thread->data->detached) {
    klee_lock_release(lock_id);
    return EINVAL;
  }

  thread->data->detached = true;

  if (thread->state != KPR_THREAD_STATE_LIVE) {
    // So we now have to basically do a cleanup as with join
    free(thread->data);
    thread->data = NULL;
  }

  klee_lock_release(lock_id);

  return 0;
}

void pthread_exit(void* arg) {
  struct kpr_thread* thread = pthread_self();
  klee_sync_primitive_t* lock_id = &thread->lock;

  klee_lock_acquire(lock_id);

  assert(thread->state == KPR_THREAD_STATE_LIVE && "Thread cannot have called exit twice");

  thread->state = KPR_THREAD_STATE_EXITED;

  while (kpr_list_size(&thread->data->cleanupStack) > 0) {
    pthread_cleanup_pop(1);
  }

  kpr_key_clear_data_of_thread();

  if (!thread->data->detached) {
    thread->data->returnValue = arg;

    klee_cond_signal(&thread->data->joinCond);
  } else if (ownThread != NULL) {
    // ownThread is NULL for the main thread
    // -> the main thread has the `thread` variable as a global object
    free(thread->data);
    thread->data = NULL;
  }

  // Lock will be released just before the thread actually exits
  klee_exit_thread(lock_id);
}

int pthread_join(pthread_t pthread, void **ret) {
  struct kpr_thread* thread = pthread;
  klee_sync_primitive_t* lock_id = &thread->lock;

  klee_lock_acquire(lock_id);

  if (thread->data->detached) { // detached state
    klee_lock_release(lock_id);
    return EINVAL;
  }

  if (pthread_self() == pthread) { // We refer to our own thread
    klee_lock_release(lock_id);
    return EDEADLK;
  }

  while (thread->state != KPR_THREAD_STATE_EXITED) {
    klee_cond_wait(&thread->data->joinCond, lock_id);
  }

  klee_por_thread_join(thread);

  if (ret != NULL) {
    // if (thread->cancelSignalReceived == 1) {
    //  *ret = PTHREAD_CANCELED;
    // } else {
      // If we have returned, then we should be able to return the data
      *ret = thread->data->returnValue;
    // }
  }

  free(thread->data);
  thread->data = NULL;

  klee_lock_release(lock_id);

  return 0;
}

void pthread_cleanup_pop(int execute) {
  pthread_t thread = pthread_self();

  assert(kpr_list_size(&thread->data->cleanupStack) > 0);
  kpr_cleanup_data* data = (kpr_cleanup_data*) kpr_list_pop(&thread->data->cleanupStack);

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

  kpr_list_push(&thread->data->cleanupStack, data);
}

int kpr_signal_thread(pthread_t th) {
  klee_cond_signal(&th->data->selfWaitCond);
  return 0;
}

int kpr_wait_thread_self(klee_sync_primitive_t* lock) {
  klee_cond_wait(&pthread_self()->data->selfWaitCond, lock);
  return 0;
}
