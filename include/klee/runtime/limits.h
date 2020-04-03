#ifndef LIMITS_WRAPPER_H
#define LIMITS_WRAPPER_H

// This is a fix to build the ublibc with clang and our mock headers
#ifndef _LIBC_LIMITS_H_
#include_next <limits.h>
#endif

// Ensure that we always enforce our limit
#ifdef SEM_VALUE_MAX
#undef SEM_VALUE_MAX
#endif

#define SEM_VALUE_MAX (32767)

#endif // LIMITS_WRAPPER_H