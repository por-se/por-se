#ifndef KPR_RUNTIME_LOCK_H
#define KPR_RUNTIME_LOCK_H

#include "klee/klee.h"

void kpr_acquire_runtime_lock();
void kpr_release_runtime_lock();

klee_sync_primitive_t* kpr_runtime_lock();

#endif
