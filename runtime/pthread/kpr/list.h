#ifndef KPR_LIST_H
#define KPR_LIST_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct kpr_list_node {
  struct kpr_list_node* prev;
  struct kpr_list_node* next;
  void* data;
} kpr_list_node;

typedef struct {
  kpr_list_node* tail;
  kpr_list_node* head;
  size_t size;
} kpr_list;

#define KPR_LIST_INITIALIZER { NULL, NULL, 0 }

typedef struct {
  kpr_list_node* current;
  kpr_list_node* next;
} kpr_list_iterator;

void kpr_list_create(kpr_list* stack);
void kpr_list_clear(kpr_list* stack);

void kpr_list_push(kpr_list* stack, void * data);
void* kpr_list_pop(kpr_list* stack);
void kpr_list_unshift(kpr_list* stack, void * data);
void* kpr_list_shift(kpr_list* stack);
size_t kpr_list_size(kpr_list* stack);

kpr_list_iterator kpr_list_iterate(kpr_list* stack);
bool kpr_list_iterator_valid(kpr_list_iterator it);
void kpr_list_iterator_next(kpr_list_iterator* it);
void* kpr_list_iterator_value(kpr_list_iterator it);

void kpr_list_erase(kpr_list* stack, kpr_list_iterator* it);

#endif // KPR_LIST_H
