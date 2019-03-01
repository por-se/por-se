#ifndef LIMITS_WRAPPER_H
#define LIMITS_WRAPPER_H

#include_next <limits.h>

// Ensure that we always enforce our limit
#ifdef SEM_VALUE_MAX
#undef SEM_VALUE_MAX
#endif

#define SEM_VALUE_MAX (32767)

#endif // LIMITS_WRAPPER_H