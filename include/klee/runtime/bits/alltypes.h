#ifndef ALLTYPES_WRAPPER_H
#define ALLTYPES_WRAPPER_H

// We do not want to use the musl defined types but instead we want to use our own ones
#define __DEFINED_pthread_t
#define __DEFINED_pthread_once_t
#define __DEFINED_pthread_key_t
#define __DEFINED_pthread_spinlock_t
#define __DEFINED_pthread_mutexattr_t
#define __DEFINED_pthread_condattr_t
#define __DEFINED_pthread_barrierattr_t
#define __DEFINED_pthread_rwlockattr_t

#include "../pthread.h"

#include_next <bits/alltypes.h>

#endif