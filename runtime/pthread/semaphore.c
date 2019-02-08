#include "klee/klee.h"
#include "klee/runtime/semaphore.h"

#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <fcntl.h>

#include "kpr/list.h"

static kpr_list openSemaphores = KPR_LIST_INITIALIZER;

static inline void kpr_sem_init(sem_t *sem, unsigned int value) {
  sem->value = value;
  sem->name = NULL;
  sem->waiting = 0;
}

int sem_init (sem_t *sem, int kpr_pshared, unsigned int value) {
  if (value > SEM_VALUE_MAX) {
    errno = EINVAL;
    return -1;
  }

  kpr_sem_init(sem, value);

  return 0;
}

/* Free resources associated with semaphore object SEM.  */
int sem_destroy (sem_t *sem) {
  if (sem->waiting > 0) {
    return -1;
  }

  return 0;
}

/* Open a named semaphore NAME with open flaot OFLAG.  */
sem_t *sem_open (__const char *__name, int __oflag, ...) {
  va_list ap;
  klee_toggle_thread_scheduling(0);

  sem_t* sem = NULL;

  kpr_list_iterator it = kpr_list_iterate(&openSemaphores);
  for (; kpr_list_iterator_valid(it); kpr_list_iterator_next(&it)) {
    sem_t* s = kpr_list_iterator_value(it);

    if (s->name == NULL) {
      continue;
    }

    if (strcmp(__name, s->name) != 0) {
      continue;
    }

    sem = s;
    break;
  }

  bool createSet = (__oflag & O_CREAT) != 0;
  bool exclSet = (__oflag & O_EXCL) != 0;

  if (sem == NULL) {
    if (!createSet) {
      errno = ENOENT;
      klee_toggle_thread_scheduling(1);
      return SEM_FAILED;
    }

    va_start(ap, __oflag);
    /*mode_t mode = */ va_arg(ap, mode_t);
    unsigned int value = va_arg(ap, unsigned int);
    va_end(ap);

    if (value > SEM_VALUE_MAX) {
      errno = EINVAL;
      klee_toggle_thread_scheduling(1);
      return SEM_FAILED;
    }

    sem = calloc(sizeof(sem_t), 1);
    kpr_sem_init(sem, value);
    sem->name = __name;

    kpr_list_push(&openSemaphores, sem);
  } else {
    if (createSet && exclSet) {
      // We are trying to create a semaphore that we already created
      errno = EEXIST;
      klee_toggle_thread_scheduling(1);
      return SEM_FAILED;
    }
  }

  klee_toggle_thread_scheduling(1);
  return sem;
}

/* Close descriptor for named semaphore SEM.  */
int sem_close (sem_t *sem) {
  return sem_unlink(sem->name);
}

/* Remove named semaphore NAME.  */
int sem_unlink (__const char *__name) {
  sem_t* sem = NULL;

  kpr_list_iterator it = kpr_list_iterate(&openSemaphores);
  for (; kpr_list_iterator_valid(it); kpr_list_iterator_next(&it)) {
    sem_t* s = kpr_list_iterator_value(it);

    if (s->name == NULL) {
      continue;
    }

    if (strcmp(__name, s->name) != 0) {
      continue;
    }

    sem = s;

    // If we found it then we should no longer allow to open the semaphore
    kpr_list_erase(&openSemaphores, &it);

    break;
  }

  if (sem == NULL) {
    klee_toggle_thread_scheduling(1);
    return ENOENT;
  }

  return 0;
}

static int kpr_sem_trywait(sem_t* sem) {
  if (sem->value <= 0) {
    return EAGAIN;
  }

  sem->value--;
  return 0;
}

/* Wait for SEM being posted.  */
int sem_wait (sem_t *sem) {
  klee_toggle_thread_scheduling(0);

  int result = 0;
  while (1) {
    result = kpr_sem_trywait(sem);

    if (result == EAGAIN) {
      sem->waiting++;
      klee_wait_on(sem);
    } else {
      break;
    }
  }

  klee_toggle_thread_scheduling(1);

  return result;
}

/* Test whether SEM is posted.  */
int sem_trywait (sem_t *sem) {
  klee_toggle_thread_scheduling(0);

  int result = kpr_sem_trywait(sem);

  klee_toggle_thread_scheduling(1);

  if (result == 0) {
    return result;
  } else {
    errno = result;
    return -1;
  }
}

/* Post SEM.  */
int sem_post (sem_t *sem) {
  klee_toggle_thread_scheduling(0);

  if (sem->value == SEM_VALUE_MAX) {
    klee_toggle_thread_scheduling(1);
    errno = EOVERFLOW;
    return -1;
  }

  sem->value++;
  if (sem->value > 0) {
    // We can wake up all threads since our wait impl will rewait
    if (sem->waiting > 0) {
      sem->waiting--;
      klee_release_waiting(sem, KLEE_RELEASE_SINGLE);
    }

    klee_toggle_thread_scheduling(1);
    klee_preempt_thread();
  } else {
    klee_toggle_thread_scheduling(1);
  }

  return 0;
}

/* Get current value of SEM and store it in *SVAL.  */
int sem_getvalue (sem_t *sem, int * kpr_sval) {
  klee_toggle_thread_scheduling(0);

  *kpr_sval = sem->value;

  klee_toggle_thread_scheduling(1);

  return 0;
}
