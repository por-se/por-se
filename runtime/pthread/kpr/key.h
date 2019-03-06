#ifndef KPR_KEY_H
#define KPR_KEY_H

#include <stdint.h>

#include "list.h"

typedef struct {
  uint32_t thread;
  void* value;
} kpr_key_data;

typedef struct {
  void (*destructor)(void*);
  kpr_list values;
} kpr_key;

#endif // KPR_KEY_H
