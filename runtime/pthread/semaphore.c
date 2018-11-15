#include <semaphore.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <fcntl.h>

#include "klee/klee.h"
#include "pthread_impl.h"
#include "utils.h"

static kpr_list openSemaphores = KPR_LIST_INITIALIZER;

static kpr_semaphore* kpr_obtain_semaphore(sem_t *sem) {
  return *((kpr_semaphore**)sem);
}

static kpr_semaphore* kpr_sem_create(sem_t *kpr_sem, unsigned int kpr_value) {
  kpr_semaphore* sem = malloc(sizeof(kpr_semaphore));
  memset(sem, 0, sizeof(kpr_semaphore));

  kpr_list_create(&sem->waiting);
  sem->value = kpr_value;
  sem->name = NULL;

  *((kpr_semaphore**)kpr_sem) = sem;

  return sem;
}

int sem_init (sem_t *kpr_sem, int kpr_pshared, unsigned int kpr_value) {
  klee_toggle_thread_scheduling(0);

  if (kpr_value > SEM_VALUE_MAX) {
    errno = EINVAL;
    klee_toggle_thread_scheduling(1);
    return -1;
  }

  kpr_sem_create(kpr_sem, kpr_value);

  klee_toggle_thread_scheduling(1);
  return 0;
}

/* Free resources associated with semaphore object SEM.  */
int sem_destroy (sem_t *kpr_sem) {
  klee_toggle_thread_scheduling(0);

  kpr_semaphore* sem = kpr_obtain_semaphore(kpr_sem);
  if (kpr_list_size(&sem->waiting) != 0) {
    klee_toggle_thread_scheduling(1);

    return -1;
  }

  free(sem);

  return 0;
}

/* Open a named semaphore NAME with open flaot OFLAG.  */
sem_t *sem_open (__const char *__name, int __oflag, ...) {
  va_list ap;
  klee_toggle_thread_scheduling(0);

  kpr_semaphore* sem = NULL;
  sem_t* retSem = NULL;

  kpr_list_iterator it = kpr_list_iterate(&openSemaphores);
  for (; kpr_list_iterator_valid(it); kpr_list_iterator_next(&it)) {
    kpr_semaphore* s = kpr_list_iterator_value(it);

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
    mode_t mode = va_arg(ap, mode_t);
    unsigned int value = va_arg(ap, unsigned int);
    va_end(ap);

    if (value > SEM_VALUE_MAX) {
      errno = EINVAL;
      klee_toggle_thread_scheduling(1);
      return SEM_FAILED;
    }

    retSem = malloc(sizeof(sem));
    sem = kpr_sem_create(retSem, value);
    sem->name = __name;
  } else {
    if (createSet && exclSet) {
      // We are trying to create a semaphore that we already created
      errno = EEXIST;
      klee_toggle_thread_scheduling(1);
      return SEM_FAILED;
    }

    retSem = malloc(sizeof(sem));
    *((kpr_semaphore**)retSem) = sem;
  }

  klee_toggle_thread_scheduling(1);
  return retSem;
}

/* Close descriptor for named semaphore SEM.  */
int sem_close (sem_t *kpr_sem) {
  kpr_semaphore* sem = kpr_obtain_semaphore(kpr_sem);
  sem_unlink(sem->name);
  return 0;
}

/* Remove named semaphore NAME.  */
int sem_unlink (__const char *__name) {
  kpr_semaphore* sem = NULL;

  kpr_list_iterator it = kpr_list_iterate(&openSemaphores);
  for (; kpr_list_iterator_valid(it); kpr_list_iterator_next(&it)) {
    kpr_semaphore* s = kpr_list_iterator_value(it);

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

static int kpr_sem_trywait(kpr_semaphore* sem) {
  if (sem->value <= 0) {
    return EAGAIN;
  }

  sem->value--;
  return 0;
}

/* Wait for SEM being posted.  */
int sem_wait (sem_t *kpr_sem) {
  klee_toggle_thread_scheduling(0);

  kpr_semaphore* sem = kpr_obtain_semaphore(kpr_sem);

  int result = 0;
  while (1) {
    result = kpr_sem_trywait(sem);

    if (result == EAGAIN) {
      kpr_list_push(&sem->waiting, (void*) klee_get_thread_id());
      // klee_toggle_thread_scheduling(1);
      klee_sleep_thread();
      // klee_toggle_thread_scheduling(0);
    } else {
      break;
    }
  }

  klee_toggle_thread_scheduling(1);

  return result;
}

/* Test whether SEM is posted.  */
int sem_trywait (sem_t *kpr_sem) {
  klee_toggle_thread_scheduling(0);

  kpr_semaphore* sem = kpr_obtain_semaphore(kpr_sem);
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
int sem_post (sem_t *kpr_sem) {
  klee_toggle_thread_scheduling(0);

  kpr_semaphore* sem = kpr_obtain_semaphore(kpr_sem);
  if (sem->value == SEM_VALUE_MAX) {
    klee_toggle_thread_scheduling(1);
    errno = EOVERFLOW;
    return -1;
  }

  sem->value++;
  if (sem->value > 0) {
    // We can wake up all threads since our wait impl will rewait
    kpr_notify_threads(&sem->waiting);
    klee_toggle_thread_scheduling(1);

    klee_preempt_thread();
  } else {
    klee_toggle_thread_scheduling(1);
  }

  return 0;
}

/* Get current value of SEM and store it in *SVAL.  */
int sem_getvalue (sem_t *kpr_sem, int * kpr_sval) {
  klee_toggle_thread_scheduling(0);

  kpr_semaphore* sem = kpr_obtain_semaphore(kpr_sem);
  *kpr_sval = sem->value;

  klee_toggle_thread_scheduling(1);

  return 0;
}
