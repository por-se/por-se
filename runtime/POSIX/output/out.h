#ifndef _POSIX_OUT
#define _POSIX_OUT

#include <stdlib.h>

#define KPR_OUTPUT_STDOUT (1)
#define KPR_OUTPUT_STDERR (2)

ssize_t kpr_output(int target, const char* buffer, size_t count);

#endif // _POSIX_OUT
