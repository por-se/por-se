#ifndef LIMITS_WRAPPER_H
#define LIMITS_WRAPPER_H

#include_next <limits.h>

#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX (32767)
#endif

#endif // LIMITS_WRAPPER_H