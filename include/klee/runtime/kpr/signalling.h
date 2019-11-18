#ifndef KPR_SIGNALLING_H
#define KPR_SIGNALLING_H

#include <pthread.h>

int kpr_signal_thread(pthread_t th);
int kpr_wait_thread_self(pthread_mutex_t* mutex);

#endif // KPR_SIGNALLING_H
