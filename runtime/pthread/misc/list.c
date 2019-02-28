#include "klee/klee.h"
#include "../kpr/list.h"

#include <stdlib.h>
#include <errno.h>

void kpr_list_create(kpr_list* stack) {
  stack->size = 0;
  stack->tail = NULL;
  stack->head = NULL;
}

void kpr_list_clear(kpr_list* stack) {
  kpr_list_node* n = (kpr_list_node*) stack->head;
  while (n != NULL) {
    kpr_list_node* newN = n->next;
    free(n);
    n = newN;
  }

  stack->head = NULL;
  stack->tail = NULL;
  stack->size = 0;
}

void kpr_list_push(kpr_list* stack, void * data) {
  kpr_list_node* newTail = calloc(sizeof(kpr_list_node), 1);

  newTail->data = data;
  newTail->prev = (kpr_list_node*) stack->tail;

  if (newTail->prev != NULL) {
    newTail->prev->next = newTail;
  } else {
    stack->head = newTail;
  }

  stack->tail = newTail;
  stack->size++;
}

void* kpr_list_pop(kpr_list* stack) {
  if (stack->size == 0) {
    klee_warning("Invalid pop; there was no data");
    return NULL;
  }

  kpr_list_node* top = (kpr_list_node*) stack->tail;
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

void kpr_list_unshift(kpr_list* stack, void * data) {
  kpr_list_node* newHead = calloc(sizeof(kpr_list_node), 1);

  newHead->data = data;

  newHead->next = (kpr_list_node*) stack->head;
  if (newHead->next != NULL) {
    newHead->next->prev = newHead;
  }

  stack->head = newHead;
  stack->size++;
}

void* kpr_list_shift(kpr_list* stack) {
  if (stack->size == 0) {
    klee_warning("Invalid shift; there was no data");
    return NULL;
  }

  kpr_list_node* head = (kpr_list_node*) stack->head;
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

size_t kpr_list_size(kpr_list* stack) {
  return stack->size;
}

kpr_list_iterator kpr_list_iterate(kpr_list* stack) {
  kpr_list_iterator it = {
          stack->head,
          NULL
  };

  return it;
}

bool kpr_list_iterator_valid(kpr_list_iterator it) {
  return it.current != NULL || it.next != NULL;
}

void kpr_list_iterator_next(kpr_list_iterator* it) {
  if (it->next == NULL) {
    it->current = it->current->next;
  } else {
    it->current = it->next;
    it->next = NULL;
  }
}

void* kpr_list_iterator_value(kpr_list_iterator it) {
  return it.current == NULL ? NULL : it.current->data;
}

void kpr_list_erase(kpr_list* stack, kpr_list_iterator* it) {
  // So this method should erase the current iterator and update its value
  if (it->current == NULL) {
    klee_warning("Erasing iterator that does not exist");
    return;
  }

  kpr_list_node* nodeToDelete = it->current;

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

  if (stack->head == nodeToDelete) {
    stack->head = nodeToDelete->next;
  }

  if (stack->tail == nodeToDelete) {
    stack->tail = nodeToDelete->prev;
  }

  stack->size--;

  free(nodeToDelete);
}
