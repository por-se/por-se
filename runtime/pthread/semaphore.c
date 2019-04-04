#include "klee/klee.h"
#include "klee/runtime/semaphore.h"

#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>

#include "kpr/list.h"

static kpr_list openSemaphores = KPR_LIST_INITIALIZER;
static pthread_mutex_t openSemaphoresLock = PTHREAD_MUTEX_INITIALIZER;

static inline void kpr_sem_init(sem_t *sem, unsigned int value) {
  sem->value = value;
  sem->name = NULL;
  sem->waiting = 0;

  pthread_mutex_init(&sem->mutex, NULL);
  pthread_cond_init(&sem->cond, NULL);
}

int sem_init (sem_t *sem, int kpr_pshared, unsigned int value) {
  if (value > SEM_VALUE_MAX) {
    errno = EINVAL;
    return -1;
  }

  kpr_sem_init(sem, value);

  return 0;
}

int sem_destroy (sem_t *sem) {
  if (sem->waiting > 0) {
    errno = EBUSY;
    return -1;
  }

  if (sem->name != NULL) {
    errno = EINVAL;
    return -1;
  }

  pthread_mutex_destroy(&sem->mutex);
  pthread_cond_destroy(&sem->cond);

  return 0;
}

static kpr_list_iterator find_sem_by_name(const char* name) {
  kpr_list_iterator it = kpr_list_iterate(&openSemaphores);

  for (; kpr_list_iterator_valid(it); kpr_list_iterator_next(&it)) {
    sem_t* s = kpr_list_iterator_value(it);

    if (s->name == NULL) {
      continue;
    }

    if (name != s->name || strcmp(name, s->name) != 0) {
      continue;
    }

    break;
  }

  return it;
}

sem_t *sem_open (__const char *__name, int __oflag, ...) {
  va_list ap;
  pthread_mutex_lock(&openSemaphoresLock);

  sem_t* sem = NULL;
  kpr_list_iterator it = find_sem_by_name(__name);
  if (kpr_list_iterator_valid(it)) {
    sem = kpr_list_iterator_value(it);
  }

  bool createSet = (__oflag & O_CREAT) != 0;
  bool exclSet = (__oflag & O_EXCL) != 0;

  // there is already a valid semaphore
  if (sem != NULL) {
    pthread_mutex_unlock(&openSemaphoresLock);

    if (createSet && exclSet) {
      // We are trying to create a semaphore that we already created
      errno = EEXIST;
      return SEM_FAILED;
    }

    return sem;
  }

  // So there is not a semaphore with that name
  if (!createSet) {
    errno = ENOENT;
    pthread_mutex_unlock(&openSemaphoresLock);
    return SEM_FAILED;
  }

  // These are required, in the case that they are missing, KLEE exits with an
  // out of bound pointer error
  va_start(ap, __oflag);
  /*mode_t mode = */ va_arg(ap, mode_t);
  unsigned int value = va_arg(ap, unsigned int);
  va_end(ap);

  if (value > SEM_VALUE_MAX) {
    errno = EINVAL;
    pthread_mutex_unlock(&openSemaphoresLock);
    return SEM_FAILED;
  }

  sem = calloc(sizeof(sem_t), 1);
  kpr_sem_init(sem, value);
  sem->name = __name;

  kpr_list_push(&openSemaphores, sem);

  pthread_mutex_unlock(&openSemaphoresLock);
  return sem;
}

int sem_close (sem_t *sem) {
  return sem_unlink(sem->name);
}

int sem_unlink (__const char *__name) {
  sem_t* sem = NULL;

  pthread_mutex_lock(&openSemaphoresLock);

  kpr_list_iterator it = find_sem_by_name(__name);
  if (kpr_list_iterator_valid(it)) {
    sem = kpr_list_iterator_value(it);
    kpr_list_erase(&openSemaphores, &it);
  }

  pthread_mutex_unlock(&openSemaphoresLock);

  if (sem == NULL) {
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

int sem_wait (sem_t *sem) {
  pthread_mutex_lock(&sem->mutex);

  int result = 0;
  while (1) {
    result = kpr_sem_trywait(sem);

    if (result == EAGAIN) {
      sem->waiting++;
      pthread_cond_wait(&sem->cond, &sem->mutex);
    } else {
      break;
    }
  }

  pthread_mutex_unlock(&sem->mutex);

  return result;
}

int sem_trywait (sem_t *sem) {
  pthread_mutex_lock(&sem->mutex);

  int result = kpr_sem_trywait(sem);

  pthread_mutex_unlock(&sem->mutex);

  if (result == 0) {
    return result;
  } else {
    errno = result;
    return -1;
  }
}

int sem_post (sem_t *sem) {
  pthread_mutex_lock(&sem->mutex);

  if (sem->value == SEM_VALUE_MAX) {
    pthread_mutex_unlock(&sem->mutex);
    errno = EOVERFLOW;
    return -1;
  }

  sem->value++;
  if (sem->value > 0) {
    // We can wake up all threads since our wait impl will rewait
    if (sem->waiting > 0) {
      sem->waiting--;

      pthread_cond_signal(&sem->cond);
    }
  }

  pthread_mutex_unlock(&sem->mutex);

  return 0;
}

int sem_getvalue (sem_t *sem, int * kpr_sval) {
  pthread_mutex_lock(&sem->mutex);

  *kpr_sval = sem->value;

  pthread_mutex_unlock(&sem->mutex);

  return 0;
}
