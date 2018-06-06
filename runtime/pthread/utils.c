#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

void __stack_create(__pthread_impl_stack* stack) {
  stack->size = 0;
  stack->top = NULL;
}

void __stack_push(__pthread_impl_stack* stack, void * data) {
  __pthread_impl_stack_node* newTop = malloc(sizeof(__pthread_impl_stack_node));
  memset(newTop, 0, sizeof(struct __pthread_impl_stack_node));

  newTop->data = data;
  newTop->prev = stack->top;
  stack->top = newTop;
  stack->size++;
}

void* __stack_pop(__pthread_impl_stack* stack) {
  __pthread_impl_stack_node* top = stack->top;
  stack->top = top->prev;
  stack->size--;
  void* data = top->data;
  free(top);
  return data;
}

size_t __stack_size(__pthread_impl_stack* stack) {
  return stack->size;
}

void __notify_threads(__pthread_impl_stack* stack) {
  size_t size = __stack_size(stack);

  for (size_t i = 0; i < size; ++i) {
    uint64_t data = (uint64_t) __stack_pop(stack);
    klee_wake_up_thread(data);
  }
}

//
//int pthread_getschedparam(pthread_t, int *__restrict, struct sched_param *__restrict);
//int pthread_setschedparam(pthread_t, int, const struct sched_param *);
//int pthread_setschedprio(pthread_t, int);
//
//int pthread_cond_init(pthread_cond_t *__restrict, const pthread_condattr_t *__restrict);
//int pthread_cond_destroy(pthread_cond_t *);
//int pthread_cond_wait(pthread_cond_t *__restrict, pthread_mutex_t *__restrict);
//int pthread_cond_timedwait(pthread_cond_t *__restrict, pthread_mutex_t *__restrict, const struct timespec *__restrict);
//int pthread_cond_broadcast(pthread_cond_t *);
//int pthread_cond_signal(pthread_cond_t *);
//
//int pthread_key_create(pthread_key_t *, void (*)(void *));
//int pthread_key_delete(pthread_key_t);
//void *pthread_getspecific(pthread_key_t);
//int pthread_setspecific(pthread_key_t, const void *);
//
//int pthread_attr_init(pthread_attr_t *);
//int pthread_attr_destroy(pthread_attr_t *);
//
//int pthread_attr_getguardsize(const pthread_attr_t *__restrict, size_t *__restrict);
//int pthread_attr_setguardsize(pthread_attr_t *, size_t);
//int pthread_attr_getstacksize(const pthread_attr_t *__restrict, size_t *__restrict);
//int pthread_attr_setstacksize(pthread_attr_t *, size_t);
//int pthread_attr_getdetachstate(const pthread_attr_t *, int *);
//int pthread_attr_setdetachstate(pthread_attr_t *, int);
//int pthread_attr_getstack(const pthread_attr_t *__restrict, void **__restrict, size_t *__restrict);
//int pthread_attr_setstack(pthread_attr_t *, void *, size_t);
//int pthread_attr_getscope(const pthread_attr_t *__restrict, int *__restrict);
//int pthread_attr_setscope(pthread_attr_t *, int);
//int pthread_attr_getschedpolicy(const pthread_attr_t *__restrict, int *__restrict);
//int pthread_attr_setschedpolicy(pthread_attr_t *, int);
//int pthread_attr_getschedparam(const pthread_attr_t *__restrict, struct sched_param *__restrict);
//int pthread_attr_setschedparam(pthread_attr_t *__restrict, const struct sched_param *__restrict);
//int pthread_attr_getinheritsched(const pthread_attr_t *__restrict, int *__restrict);
//int pthread_attr_setinheritsched(pthread_attr_t *, int);
//
//int pthread_condattr_init(pthread_condattr_t *);
//int pthread_condattr_destroy(pthread_condattr_t *);
//int pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
//int pthread_condattr_setpshared(pthread_condattr_t *, int);
//int pthread_condattr_getclock(const pthread_condattr_t *__restrict, clockid_t *__restrict);
//int pthread_condattr_getpshared(const pthread_condattr_t *__restrict, int *__restrict);

//
//int pthread_atfork(void (*)(void), void (*)(void), void (*)(void));
//
//int pthread_getconcurrency(void);
//int pthread_setconcurrency(int);
//
//int pthread_getcpuclockid(pthread_t, clockid_t *);