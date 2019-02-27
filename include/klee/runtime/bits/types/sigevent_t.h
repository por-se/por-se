#ifndef sigevent_wrapper_h
#define sigevent_wrapper_h

// So the glibc header uses the pthread_attr_t type
#define __have_pthread_attr_t 1
#include "../../pthread.h"

// Include the system header(!) file
// XXX: Note that include_next is GNU extension
#include_next <bits/types/sigevent_t.h>

#endif // sigevent_wrapper_h