#ifndef KLEE_PTHREAD_IMPL_KEY_H
#define KLEE_PTHREAD_IMPL_KEY_H

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "utils.h"

typedef struct {
  uint64_t thread;
  void* value;
} __pthread_key_data;

typedef struct {
  void (*destructor)(void*);
  __kpr_list values;
} __pthread_key;

void __pthread_key_clear_data_of_thread(uint64_t thread);

#endif //KLEE_PTHREAD_IMPL_KEY_H
