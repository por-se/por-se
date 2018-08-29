#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static __pthread_impl_pthread* __obtain_pthread_data(pthread_t pthread) {
  return ((__pthread_impl_pthread*)pthread);
}

static void* __pthread_impl_wrapper(void* arg, uint64_t tid) {
  klee_toggle_thread_scheduling(0);
  __pthread_impl_pthread* thread = arg;
  thread->tid = tid;
  void* startArg = thread->startArg;
  klee_toggle_thread_scheduling(1);

  void* ret = thread->startRoutine(startArg);
  pthread_exit(ret);
}

int pthread_create(pthread_t *pthread, const pthread_attr_t *attr, void *(*startRoutine)(void *), void *arg) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_pthread* thread = malloc(sizeof(__pthread_impl_pthread));
  if (thread == NULL) {
    klee_toggle_thread_scheduling(1);
    return EAGAIN;
  }
  memset(thread, 0, sizeof(__pthread_impl_mutex));

  *((__pthread_impl_pthread**)pthread) = thread;

  thread->tid = 0;
  thread->startRoutine = startRoutine;
  thread->startArg = arg;
  thread->returnValue = NULL;
  thread->state = 0;
  thread->mode = 0;
  thread->joinState = 0;
  thread->cancelState = PTHREAD_CANCEL_ENABLE;

  if (attr != NULL) {
    int ds = 0;
    pthread_attr_getdetachstate(attr, &ds);

    if (ds == PTHREAD_CREATE_DETACHED) {
      thread->mode = 1;
    }
  }

  klee_toggle_thread_scheduling(1);

  klee_create_thread(__pthread_impl_wrapper, thread);
  klee_preempt_thread();

  return 0;
}

int pthread_detach(pthread_t pthread) {
  if (pthread == 0) {
    return 0;
  }

  klee_toggle_thread_scheduling(0);

  __pthread_impl_pthread* thread = __obtain_pthread_data(pthread);
  if (thread->mode == 1) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  thread->mode = 1;

  klee_toggle_thread_scheduling(1);
  klee_preempt_thread();

  return 0;
}

void pthread_exit(void* arg) {
  klee_toggle_thread_scheduling(0);
  uint64_t tid = klee_get_thread_id();

  if (tid != 0) {
    __pthread_impl_pthread* thread = (__pthread_impl_pthread*) klee_get_thread_start_argument();

    if (thread->mode == 1) {
      klee_toggle_thread_scheduling(1);
      klee_exit_thread();
    }

    thread->returnValue = arg;
    thread->state = 1;

    if (thread->joinState == 0) {
      klee_toggle_thread_scheduling(1);
      klee_sleep_thread();
      thread->joinState = 1;
      klee_toggle_thread_scheduling(0);
    } else {
      klee_wake_up_thread(thread->joinedThread);
    }
  }

  klee_toggle_thread_scheduling(1);
  klee_exit_thread();
}

int pthread_join(pthread_t pthread, void **ret) {
  klee_toggle_thread_scheduling(0);
  __pthread_impl_pthread* thread = __obtain_pthread_data(pthread);

  if (thread->mode == 1) { // detached state
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  uint64_t ownThread = klee_get_thread_id();
  if (ownThread == (uint64_t) pthread) { // We refer to our onw thread
    klee_toggle_thread_scheduling(1);
    return EDEADLK;
  }

  if (thread->joinState == 1) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

  // Could also be that this thread is already finished, but there must be
  // at least one call to join to free the resources
  thread->joinState = 1;

  int needToSleep = thread->state != 1 ? 1 : 0;

  if (needToSleep == 1) {
    thread->joinedThread = ownThread;
    klee_toggle_thread_scheduling(1);
    klee_sleep_thread();
    klee_toggle_thread_scheduling(0);
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
    __pthread_impl_pthread* thread = (__pthread_impl_pthread*) klee_get_thread_start_argument;
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

  __pthread_impl_pthread* thread = (__pthread_impl_pthread*) klee_get_thread_start_argument;
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
  klee_toggle_thread_scheduling(0);
  __pthread_impl_pthread* thread = (__pthread_impl_pthread*) tid;
  thread->cancelSignalReceived = 1;
  klee_toggle_thread_scheduling(1);

  return 0;
}

static pthread_once_t __ONCE_STATIC_INIT = PTHREAD_ONCE_INIT;

int pthread_once(pthread_once_t *o, void (*func)(void)) {
  klee_toggle_thread_scheduling(0);

  int* onceAsValue = (int*) o;

  if (!__checkIfSameSize((char*) o, (char*) &__ONCE_STATIC_INIT)) {
    klee_toggle_thread_scheduling(1);
    return EINVAL;
  }

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

static int __pthread_impl_concurrency = 0;

int pthread_getconcurrency(void) {
  return __pthread_impl_concurrency;
}

int pthread_setconcurrency(int n) {
  if (n < 0) {
    return EINVAL;
  }

  __pthread_impl_concurrency = n;

  return 0;
}

//int pthread_getcpuclockid(pthread_t, clockid_t *);