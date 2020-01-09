#include "klee/klee.h"
#include "klee/runtime/semaphore.h"

#include "kpr/internal.h"

#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdlib.h>

#include "kpr/list.h"

static kpr_list openSemaphores = KPR_LIST_INITIALIZER;
static klee_sync_primitive_t openSemaphoresLock;

static inline void kpr_sem_init(sem_t *sem, unsigned int value) {
  kpr_check_for_double_init(sem);
  kpr_ensure_valid(sem);

  sem->value = value;
  sem->name = NULL;

  sem->waitingCount = 0;

  klee_por_register_event(por_lock_create, &sem->mutex);

  kpr_ensure_valid(sem);
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
  kpr_check_if_valid(sem_t, sem);

  if (sem->waitingCount > 0) {
    errno = EBUSY;
    return -1;
  }

  if (sem->name != NULL) {
    errno = EINVAL;
    return -1;
  }

  memset(sem, 0xAB, sizeof(sem_t));

  klee_por_register_event(por_lock_destroy, &sem->mutex);

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
  klee_lock_acquire(&openSemaphoresLock);

  sem_t* sem = NULL;
  kpr_list_iterator it = find_sem_by_name(__name);
  if (kpr_list_iterator_valid(it)) {
    sem = kpr_list_iterator_value(it);
  }

  bool createSet = (__oflag & O_CREAT) != 0;
  bool exclSet = (__oflag & O_EXCL) != 0;

  // there is already a valid semaphore
  if (sem != NULL) {
    klee_lock_release(&openSemaphoresLock);

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
    klee_lock_release(&openSemaphoresLock);
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
    klee_lock_release(&openSemaphoresLock);
    return SEM_FAILED;
  }

  sem = calloc(sizeof(sem_t), 1);
  kpr_sem_init(sem, value);
  sem->name = __name;

  kpr_list_push(&openSemaphores, sem);

  klee_lock_release(&openSemaphoresLock);
  return sem;
}

int sem_close (sem_t *sem) {
  return sem_unlink(sem->name);
}

int sem_unlink (__const char *__name) {
  sem_t* sem = NULL;

  klee_lock_acquire(&openSemaphoresLock);

  kpr_list_iterator it = find_sem_by_name(__name);
  if (kpr_list_iterator_valid(it)) {
    sem = kpr_list_iterator_value(it);
    kpr_list_erase(&openSemaphores, &it);
  }

  klee_lock_release(&openSemaphoresLock);

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
  kpr_check_if_valid(sem_t, sem);

  klee_lock_acquire(&sem->mutex);

  int result = 0;
  while (1) {
    result = kpr_sem_trywait(sem);

    if (result == EAGAIN) {
      sem->waitingCount++;
      klee_cond_wait(&sem->cond, &sem->mutex);
    } else {
      break;
    }
  }

  klee_lock_release(&sem->mutex);

  return result;
}

int sem_trywait (sem_t *sem) {
  kpr_check_if_valid(sem_t, sem);

  klee_lock_acquire(&sem->mutex);

  int result = kpr_sem_trywait(sem);

  klee_lock_release(&sem->mutex);

  if (result == 0) {
    return result;
  } else {
    errno = result;
    return -1;
  }
}

int sem_post (sem_t *sem) {
  kpr_check_if_valid(sem_t, sem);

  klee_lock_acquire(&sem->mutex);

  if (sem->value == SEM_VALUE_MAX) {
    klee_lock_release(&sem->mutex);
    errno = EOVERFLOW;
    return -1;
  }

  sem->value++;
  if (sem->value > 0) {
    if (sem->waitingCount > 0) {
      sem->waitingCount--;
      klee_cond_signal(&sem->cond);
    }
  }

  klee_lock_release(&sem->mutex);

  return 0;
}

int sem_getvalue (sem_t *sem, int * kpr_sval) {
  kpr_check_if_valid(sem_t, sem);

  klee_lock_acquire(&sem->mutex);

  *kpr_sval = sem->value;

  klee_lock_release(&sem->mutex);

  return 0;
}
