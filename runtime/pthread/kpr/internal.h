#ifndef KPR_INTERNAL_H
#define KPR_INTERNAL_H

#include "klee/runtime/pthread.h"

#include <stdint.h>

void kpr_key_clear_data_of_thread(uint64_t tid);

int kpr_mutex_unlock_internal(pthread_mutex_t *m);

#endif // KPR_INTERNAL_H