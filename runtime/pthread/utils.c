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

bool __checkIfSameSize(char* target, char* reference) {
  // So this method should check if both of these objects have the same contents
  size_t sizeOfTarget = klee_get_obj_size((void*) target);
  size_t sizeOfReference = klee_get_obj_size((void*) reference);

  return sizeOfReference == sizeOfTarget;
}

bool __checkIfSame(char* target, char* reference) {
  // So this method should check if both of these objects have the same contents
  size_t sizeOfTarget = klee_get_obj_size((void*) target);
  size_t sizeOfReference = klee_get_obj_size((void*) reference);

  if (sizeOfReference != sizeOfTarget) {
    return false;
  }

  for (size_t i = 0; i < sizeOfTarget; i++) {
    if (target[i] != reference[i]) {
      return false;
    }
  }

  return true;
}

//
//int pthread_getschedparam(pthread_t, int *__restrict, struct sched_param *__restrict);
//int pthread_setschedparam(pthread_t, int, const struct sched_param *);
//int pthread_setschedprio(pthread_t, int);
//
//int pthread_getcpuclockid(pthread_t, clockid_t *);