#ifndef KPR_INTERNAL_H
#define KPR_INTERNAL_H

#include "klee/runtime/pthread.h"

#include <stdint.h>

#define KPR_OTHER_INIT (0)
#define KPR_RANDOM_INIT (1)
#define KPR_ZERO_INIT (2)
#define KPR_INVALID (3)

void kpr_key_clear_data_of_thread(uint64_t tid);

int kpr_mutex_unlock_internal(pthread_mutex_t *m);

void kpr_check_init_pattern(pthread_internal_t* data, size_t size);

#define kpr_check_if_valid(typename, argument) kpr_check_init_pattern((pthread_internal_t*) argument, sizeof(typename));
#define kpr_ensure_valid(argument) argument->magic.magic = PTHREAD_INTERNAL_MAGIC_VALUE

#endif // KPR_INTERNAL_H