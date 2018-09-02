#ifndef KLEE_PTHREAD_UTILS_H
#define KLEE_PTHREAD_UTILS_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct __kpr_list_node {
  struct __kpr_list_node* prev;
  struct __kpr_list_node* next;
  void* data;
} __kpr_list_node;

typedef struct {
  __kpr_list_node* tail;
  __kpr_list_node* head;
  size_t size;
} __kpr_list;

#define __KPR_LIST_INITIALIZER { NULL, NULL, 0 }

typedef struct {
  __kpr_list_node* current;
  __kpr_list_node* next;
} __kpr_list_iterator;

void __kpr_list_create(__kpr_list* stack);
void __kpr_list_clear(__kpr_list* stack);

void __kpr_list_push(__kpr_list* stack, void * data);
void* __kpr_list_pop(__kpr_list* stack);
void __kpr_list_unshift(__kpr_list* stack, void * data);
void* __kpr_list_shift(__kpr_list* stack);
size_t __kpr_list_size(__kpr_list* stack);

__kpr_list_iterator __kpr_list_iterate(__kpr_list* stack);
bool __kpr_list_iterator_valid(__kpr_list_iterator it);
void __kpr_list_iterator_next(__kpr_list_iterator* it);
void* __kpr_list_iterator_value(__kpr_list_iterator it);

void __kpr_list_erase(__kpr_list* stack, __kpr_list_iterator* it);

void __notify_threads(__kpr_list* stack);

bool __checkIfSameSize(char* target, char* reference);
bool __checkIfSame(char* target, char* reference);

#endif //KLEE_PTHREAD_UTILS_H