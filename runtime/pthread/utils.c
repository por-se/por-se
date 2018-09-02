#include "klee/klee.h"
#include "pthread_impl.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

void __kpr_list_create(__kpr_list* stack) {
  stack->size = 0;
  stack->tail = NULL;
  stack->head = NULL;
}

void __kpr_list_clear(__kpr_list* stack) {
  __kpr_list_node* n = stack->head;
  while (n != NULL) {
    __kpr_list_node* newN = n->next;
    free(n);
    n = newN;
  }

  stack->head = NULL;
  stack->tail = NULL;
  stack->size = 0;
}

void __kpr_list_push(__kpr_list* stack, void * data) {
  __kpr_list_node* newTail = malloc(sizeof(__kpr_list_node));
  memset(newTail, 0, sizeof(struct __kpr_list_node));

  newTail->data = data;
  newTail->prev = stack->tail;

  if (newTail->prev != NULL) {
    newTail->prev->next = newTail;
  } else {
    stack->head = newTail;
  }

  stack->tail = newTail;
  stack->size++;
}

void* __kpr_list_pop(__kpr_list* stack) {
  klee_assert(stack->size > 0);

  __kpr_list_node* top = stack->tail;
  stack->tail = top->prev;

  if (top->prev != NULL) {
    top->next = NULL;
  } else {
    stack->head = NULL;
  }

  stack->size--;
  void* data = top->data;
  free(top);
  return data;
}

void __kpr_list_unshift(__kpr_list* stack, void * data) {
  __kpr_list_node* newHead = malloc(sizeof(__kpr_list_node));
  memset(newHead, 0, sizeof(struct __kpr_list_node));

  newHead->data = data;

  newHead->next = stack->head;
  if (newHead->next != NULL) {
    newHead->next->prev = newHead;
  }

  stack->head = newHead;
  stack->size++;
}

void* __kpr_list_shift(__kpr_list* stack) {
  klee_assert(stack->size > 0);

  __kpr_list_node* head = stack->head;
  stack->head = head->next;

  if (head->next != NULL) {
    head->next->prev = NULL;
  } else {
    stack->tail = NULL;
  }

  stack->size--;
  void* data = head->data;
  free(head);
  return data;
}

size_t __kpr_list_size(__kpr_list* stack) {
  return stack->size;
}

__kpr_list_iterator __kpr_list_iterate(__kpr_list* stack) {
  __kpr_list_iterator it = {
          stack->head,
          NULL
  };

  return it;
}

bool __kpr_list_iterator_valid(__kpr_list_iterator it) {
  return it.current != NULL || it.next != NULL;
}

void __kpr_list_iterator_next(__kpr_list_iterator* it) {
  if (it->next == NULL) {
    it->current = it->current->next;
  } else {
    it->current = it->next;
    it->next = NULL;
  }
}

void* __kpr_list_iterator_value(__kpr_list_iterator it) {
  return it.current == NULL ? NULL : it.current->data;
}

void __kpr_list_erase(__kpr_list* stack, __kpr_list_iterator* it) {
  // So this method should erase the current iterator and update its value
  __kpr_list_node* nodeToDelete = it->current;

  if (nodeToDelete->prev != NULL) {
    nodeToDelete->prev->next = nodeToDelete->next;
    it->current = nodeToDelete->prev;
  } else {
    it->current = NULL;
    it->next = nodeToDelete->next;
  }

  if (nodeToDelete->next != NULL) {
    nodeToDelete->next->prev = nodeToDelete->prev;
  }

  free(nodeToDelete);
}

/*
 * Here is the stuff that is not directly part of the data structure but rather what is needed as well
 */

void __notify_threads(__kpr_list* stack) {
  size_t size = __kpr_list_size(stack);

  for (size_t i = 0; i < size; ++i) {
    uint64_t data = (uint64_t) __kpr_list_pop(stack);
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