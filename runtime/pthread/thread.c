#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include <errno.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include "kpr/flags.h"
#include "kpr/internal.h"

static kpr_thread mainThread;
pthread_t pthread_self(void) {
  if (klee_get_thread_id() == 0) {
    // Main thread will not have any start argument so make sure that we pass the correct one
    return &mainThread;
  }

  return (pthread_t) klee_get_thread_runtime_struct_ptr();
}

int pthread_equal(pthread_t th1, pthread_t th2) {
  return th1 == th2;
}

static void kpr_wrapper(void* arg) {
  kpr_thread* thread = (kpr_thread*) arg;

  void* ret = thread->startRoutine(thread->startArg);
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
  thread->joinState = KPR_THREAD_JSTATE_JOINABLE;

// kpr_list_create(&thread->cleanUpStack);

  if (attr != NULL) {
    int ds = 0;
    pthread_attr_getdetachstate(attr, &ds);

    if (ds == PTHREAD_CREATE_DETACHED) {
      thread->mode = KPR_THREAD_MODE_DETACH;
    }
  }

  thread->tid = klee_create_thread(kpr_wrapper, thread);
  klee_preempt_thread();

  return 0;
}

int pthread_detach(pthread_t pthread) {
  klee_toggle_thread_scheduling(0);
  kpr_thread* thread = pthread;

  if (thread->mode == KPR_THREAD_MODE_DETACH || thread->state == KPR_THREAD_STATE_EXITED) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  if (thread->joinState == KPR_THREAD_JSTATE_JOINED) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  // Last check: it can also be the case that this thread is detached after it already
  //             terminated. In that case we want to ensure that we wake the thread again
  if (thread->joinState == KPR_THREAD_JSTATE_WAIT_FOR_JOIN) {
    klee_release_waiting(thread, KLEE_RELEASE_SINGLE);
  }

  thread->mode = KPR_THREAD_MODE_DETACH;

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return 0;
}

void pthread_exit(void* arg) {
  klee_toggle_thread_scheduling(0);

  kpr_thread* thread = pthread_self();
  assert(thread->state == KPR_THREAD_STATE_LIVE && "Thread cannot have called exit twice");
  thread->state = KPR_THREAD_STATE_DEAD;

  if (thread->mode == KPR_THREAD_MODE_JOIN) {
    thread->returnValue = arg;

    // Another thread has joined with us, but is still waiting
    if (thread->joinState == KPR_THREAD_JSTATE_JOINED) {
      klee_release_waiting(thread, KLEE_RELEASE_SINGLE);
    }

    // We have to wait on another thread that will wake us up
    if (thread->joinState == KPR_THREAD_JSTATE_JOINABLE) {
      thread->joinState = KPR_THREAD_JSTATE_WAIT_FOR_JOIN;
      klee_wait_on(thread);
    }
  }

  thread->state = KPR_THREAD_STATE_EXITED;

  klee_toggle_thread_scheduling(1);

  kpr_key_clear_data_of_thread(thread->tid);
  klee_exit_thread();
}

int pthread_join(pthread_t pthread, void **ret) {
  klee_toggle_thread_scheduling(0);
  kpr_thread* thread = pthread;

  if (thread->mode == KPR_THREAD_MODE_DETACH) { // detached state
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  if (klee_get_thread_id() == thread->tid) { // We refer to our own thread
    klee_toggle_thread_scheduling(1);
    return EDEADLK;
  }

  if (thread->joinState == KPR_THREAD_JSTATE_JOINED) {
    klee_report_error(__FILE__, __LINE__, "Multiple calls to pthread_join to the same target are undefined", "undef");
  }

  int alreadyPreemptedByWaiting = 0;

  if (thread->joinState == KPR_THREAD_JSTATE_JOINABLE) {
    alreadyPreemptedByWaiting = 1;
    thread->joinState = KPR_THREAD_JSTATE_JOINED;

    klee_wait_on(thread);
  } else if (thread->joinState == KPR_THREAD_JSTATE_WAIT_FOR_JOIN) {
    thread->joinState = KPR_THREAD_JSTATE_JOINED;
    klee_release_waiting(thread, KLEE_RELEASE_SINGLE);
  }

  if (ret != NULL) {
    // if (thread->cancelSignalReceived == 1) {
    //  *ret = PTHREAD_CANCELED;
    // } else {
      // If we have returned, then we should be able to return the data
      *ret = thread->returnValue;
    // }
  }

  klee_toggle_thread_scheduling(1);
  if (alreadyPreemptedByWaiting != 0) {
    klee_preempt_thread();
  }

  return 0;
}