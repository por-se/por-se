#include "klee/klee.h"
#include "pthread_impl.h"
#include "key.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

static kpr_pthread* kpr_obtain_pthread_data(pthread_t pthread) {
  return ((kpr_pthread*)pthread);
}

static void* kpr_wrapper(void* arg, uint64_t tid) {
  klee_toggle_thread_scheduling(0);
  kpr_pthread* thread = arg;
  thread->tid = tid;
  void* startArg = thread->startArg;
  klee_toggle_thread_scheduling(1);

  void* ret = thread->startRoutine(startArg);
  pthread_exit(ret);
}

int pthread_create(pthread_t *pthread, const pthread_attr_t *attr, void *(*startRoutine)(void *), void *arg) {
  klee_toggle_thread_scheduling(0);

  kpr_pthread* thread = malloc(sizeof(kpr_pthread));
  if (thread == NULL) {
    klee_toggle_thread_scheduling(1);
    return EAGAIN;
  }
  memset(thread, 0, sizeof(kpr_pthread));

  *((kpr_pthread**)pthread) = thread;

  thread->tid = 0;
  thread->startRoutine = startRoutine;
  thread->startArg = arg;
  thread->returnValue = NULL;
  thread->state = 0;
  thread->mode = KPR_THREAD_MODE_JOIN;
  thread->joinState = KPR_THREAD_JSTATE_JOINABLE;
  thread->cancelState = PTHREAD_CANCEL_ENABLE;

  kpr_list_create(&thread->cleanUpStack);

  if (attr != NULL) {
    int ds = 0;
    pthread_attr_getdetachstate(attr, &ds);

    if (ds == PTHREAD_CREATE_DETACHED) {
      thread->mode = KPR_THREAD_MODE_DETACH;
    }
  }

  klee_toggle_thread_scheduling(1);

  klee_create_thread(kpr_wrapper, thread);
  klee_preempt_thread();

  return 0;
}

int pthread_detach(pthread_t pthread) {
  if (pthread == 0) {
    return 0;
  }

  klee_toggle_thread_scheduling(0);

  kpr_pthread* thread = kpr_obtain_pthread_data(pthread);
  if (thread->mode == KPR_THREAD_MODE_DETACH) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  // Last check: it can also be the case that this thread is detached after it already
  //             terminated. In that case we want to ensure that we wake the thread again
  if (thread->joinState == KPR_THREAD_WAIT_FOR_JOIN) {
    klee_wake_up_thread(thread->tid);
  }

  thread->mode = KPR_THREAD_MODE_DETACH;

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return 0;
}

void pthread_exit(void* arg) {
  klee_toggle_thread_scheduling(0);
  uint64_t tid = klee_get_thread_id();

  if (tid != 0) {
    kpr_pthread* thread = (kpr_pthread*) klee_get_thread_start_argument();

    if (thread->mode == KPR_THREAD_MODE_DETACH) {
      klee_toggle_thread_scheduling(1);
      kpr_key_clear_data_of_thread(tid);
      klee_exit_thread();
    }

    assert(thread->joinState != KPR_THREAD_JSTATE_WAIT_FOR_JOIN && "Thread cannot have called exit twice");

    thread->returnValue = arg;
    thread->state = 1;

    if (thread->joinState == KPR_THREAD_JSTATE_JOINABLE) {
      thread->joinState = KPR_THREAD_WAIT_FOR_JOIN;

      // klee_toggle_thread_scheduling(1);
      klee_sleep_thread();
      // klee_toggle_thread_scheduling(0);
      thread->joinState = KPR_THREAD_JSTATE_JOINED;
    } else {
      klee_wake_up_thread(thread->joinedThread);
    }
  }

  klee_toggle_thread_scheduling(1);
  kpr_key_clear_data_of_thread(tid);
  klee_exit_thread();
}

int pthread_join(pthread_t pthread, void **ret) {
  klee_toggle_thread_scheduling(0);
  kpr_pthread* thread = kpr_obtain_pthread_data(pthread);

  if (thread->mode == KPR_THREAD_MODE_DETACH) { // detached state
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  uint64_t ownThread = klee_get_thread_id();
  if (ownThread == (uint64_t) pthread) { // We refer to our own thread
    klee_toggle_thread_scheduling(1);
    return EDEADLK;
  }

  if (thread->joinState == KPR_THREAD_JSTATE_JOINED) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  // Could also be that this thread is already finished, but there must be
  // at least one call to join to free the resources
  int needToSleep = thread->joinState != KPR_THREAD_WAIT_FOR_JOIN ? 1 : 0;
  thread->joinState = KPR_THREAD_JSTATE_JOINED;

  if (needToSleep == 1) {
    thread->joinedThread = ownThread;
    // klee_toggle_thread_scheduling(1);
    klee_sleep_thread();
    // klee_toggle_thread_scheduling(0);
  } else {
    klee_wake_up_thread(thread->tid);
  }

  if (ret != NULL) {
    if (thread->cancelSignalReceived == 1) {
      *ret = PTHREAD_CANCELED;
    } else {
      // If we have returned, then we should be able to return the data
      *ret = thread->returnValue;
    }
  }

  klee_toggle_thread_scheduling(1);
  if (needToSleep == 0) {
    klee_preempt_thread();
  }

  return 0;
}

pthread_t pthread_self(void) {
  if (klee_get_thread_id() == 0) {
    // Main thread will not have any start argument so make sure that we pass the correct one
    return 0;
  }

  return (pthread_t) klee_get_thread_start_argument();
}

int pthread_equal(pthread_t t1, pthread_t t2) {
  return t1 == t2 ? 1 : 0;
}

//
//#ifndef __cplusplus
//#define pthread_equal(x,y) ((x)==(y))
//#endif
//

int pthread_setcancelstate(int state, int *oldState) {
  if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE) {
    return EINVAL;
  }

  klee_toggle_thread_scheduling(0);

  uint64_t tid = klee_get_thread_id();
  if (tid != 0) {
    kpr_pthread* thread = (kpr_pthread*) klee_get_thread_start_argument;
    *oldState = thread->cancelState;
    thread->cancelState = state;
  }

  klee_toggle_thread_scheduling(1);

  return 0;
}

//int pthread_setcanceltype(int, int *);

void pthread_testcancel() {
  uint64_t tid = klee_get_thread_id();
  if (tid == 0) {
    return;
  }

  klee_toggle_thread_scheduling(0);

  kpr_pthread* thread = (kpr_pthread*) klee_get_thread_start_argument;
  if (thread->cancelState == PTHREAD_CANCEL_DISABLE) {
    klee_toggle_thread_scheduling(1);
    return;
  }

  if (thread->cancelSignalReceived == 1) {
    klee_toggle_thread_scheduling(1);
    pthread_exit(PTHREAD_CANCELED);
  }

  klee_toggle_thread_scheduling(1);
}

int pthread_cancel(pthread_t tid) {
  klee_warning_once("pthread_cancel is not correctly supported");

  klee_toggle_thread_scheduling(0);
  kpr_pthread* thread = (kpr_pthread*) tid;
  thread->cancelSignalReceived = 1;
  klee_toggle_thread_scheduling(1);

  return 0;
}

int pthread_once(pthread_once_t *o, void (*func)(void)) {
  klee_toggle_thread_scheduling(0);

  int* onceAsValue = (int*) o;

  if (*onceAsValue != 0) {
    klee_toggle_thread_scheduling(1);
    return 0;
  }

  *onceAsValue = 1;
  klee_toggle_thread_scheduling(1);

  func();

  return 0;
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
  klee_warning_once("pthread_atfork is not supported and will be completely ignored");
  return 0;
}

static int kpr_concurrency = 0;

int pthread_getconcurrency(void) {
  return kpr_concurrency;
}

int pthread_setconcurrency(int n) {
  if (n < 0) {
    return EINVAL;
  }

  klee_warning_once("pthread_setconcurrency is ignored");
  kpr_concurrency = n;

  return 0;
}

#ifndef pthread_cleanup_pop
void pthread_cleanup_pop(int execute) {
  kpr_thread thread* = (kpr_thread*) klee_get_thread_start_argument();
  size_t stackSize = kpr_list_size(&thread->cleanUpStack);

  klee_warning_once("Argument not passed for pthread_cleanup");

  if (stackSize == 0) {
    klee_abort();
  }

  void (*routine)(void*) = kpr_list_pop(&thread->cleanUpStack);

  if (execute == 0) {
    return;
  }

  // TODO: Missing arg...
  routine(&routine);
}
#endif

#ifndef pthread_cleanup_push
void pthread_cleanup_push(void (*routine)(void*), void *arg) {
  kpr_thread thread* = (kpr_thread*) klee_get_thread_start_argument();
  kpr_list_pop(&thread->cleanUpStack, routine);
  klee_warning_once("Argument not passed for pthread_cleanup");
}
#endif

//int pthread_getcpuclockid(pthread_t, clockid_t *);
