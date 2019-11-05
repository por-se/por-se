#ifndef KPR_LIST_H
#define KPR_LIST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "list-types.h"

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
