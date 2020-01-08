#ifndef KPR_SIGNALLING_H
#define KPR_SIGNALLING_H

#include "klee/klee.h"

#include <pthread.h>

int kpr_signal_thread(pthread_t th);
int kpr_wait_thread_self(klee_sync_primitive_t* lock);

#endif // KPR_SIGNALLING_H
