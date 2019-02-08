#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <time.h>

#define SEM_FAILED ((sem_t *) 0)

typedef struct {
  int value;
  uint64_t waiting;
  const char* name;
} sem_t;

int sem_init(sem_t *sem, int f, unsigned v);
int sem_destroy(sem_t * sem);

sem_t *sem_open(const char *name, int f, ...);
int sem_unlink(const char *name);
int sem_close(sem_t *sem);

int sem_getvalue(sem_t *sem, int *v);
int sem_post(sem_t *sem);
int sem_timedwait(sem_t *sem, const struct timespec *time);
int sem_trywait(sem_t *sem);
int sem_wait(sem_t *sem);


#endif // _SEMAPHORE_H