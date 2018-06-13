#include <semaphore.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "klee/klee.h"
#include "pthread_impl.h"

static __pthread_impl_semaphore* __obtain_semaphore(sem_t *sem) {
  return *((__pthread_impl_semaphore**)sem);
}

int sem_init (sem_t *__sem, int __pshared, unsigned int __value) {
  klee_toggle_thread_scheduling(0);

  if (__value > SEM_VALUE_MAX) {
    klee_toggle_thread_scheduling(1);
    errno = EINVAL;
    return -1;
  }

  __pthread_impl_semaphore* sem = malloc(sizeof(__pthread_impl_semaphore));
  memset(sem, 0, sizeof(__pthread_impl_semaphore));

  __stack_create(&sem->waiting);
  sem->value = __value;

  klee_toggle_thread_scheduling(1);
  return 0;
}

/* Free resources associated with semaphore object SEM.  */
int sem_destroy (sem_t *__sem) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_semaphore* sem = __obtain_semaphore(__sem);
  if (__stack_size(&sem->waiting) != 0) {
    klee_toggle_thread_scheduling(1);

    return -1;
  }

  free(sem);

  return 0;
}

/* Open a named semaphore NAME with open flaot OFLAG.  */
sem_t *sem_open (__const char *__name, int __oflag, ...) {
  // TODO
  return 0;
}

/* Close descriptor for named semaphore SEM.  */
int sem_close (sem_t *__sem) {
  // TODO
  return 0;
}

/* Remove named semaphore NAME.  */
int sem_unlink (__const char *__name) {
  // TODO
  return 0;
}

static int __pthread_impl_sem_trywait(__pthread_impl_semaphore* sem) {
  if (sem->value <= 0) {
    return EAGAIN;
  }

  sem->value--;
  return 0;
}

/* Wait for SEM being posted.  */
int sem_wait (sem_t *__sem) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_semaphore* sem = __obtain_semaphore(__sem);

  int result = 0;
  while (1) {
    result = __pthread_impl_sem_trywait(sem);

    if (result == EAGAIN) {
      __stack_push(&sem->waiting, (void*) pthread_self());
      klee_toggle_thread_scheduling(1);
      klee_sleep_thread();
      klee_toggle_thread_scheduling(0);
    } else {
      break;
    }
  }

  klee_toggle_thread_scheduling(1);

  return result;
}

/* Test whether SEM is posted.  */
int sem_trywait (sem_t *__sem) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_semaphore* sem = __obtain_semaphore(__sem);
  int result = __pthread_impl_sem_trywait(sem);

  klee_toggle_thread_scheduling(1);

  if (result == 0) {
    return result;
  } else {
    errno = result;
    return -1;
  }
}

/* Post SEM.  */
int sem_post (sem_t *__sem) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_semaphore* sem = __obtain_semaphore(__sem);
  if (sem->value == SEM_VALUE_MAX) {
    klee_toggle_thread_scheduling(1);
    errno = EOVERFLOW;
    return -1;
  }

  sem->value++;
  if (sem->value > 0) {
    // We can wake up all threads since our wait impl will rewait
    __notify_threads(&sem->waiting);
    klee_toggle_thread_scheduling(1);

    klee_preempt_thread();
  } else {
    klee_toggle_thread_scheduling(1);
  }

  return 0;
}

/* Get current value of SEM and store it in *SVAL.  */
int sem_getvalue (sem_t *__sem, int * __sval) {
  klee_toggle_thread_scheduling(0);

  __pthread_impl_semaphore* sem = __obtain_semaphore(__sem);
  *__sval = sem->value;

  klee_toggle_thread_scheduling(1);

  return 0;
}