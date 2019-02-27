#ifndef LOCAL_LIM_WRAPPER_H

#include_next <bits/local_lim.h>

#ifdef SEM_VALUE_MAX
#define SEM_VALUE_MAX (32767)
#endif

#endif // LOCAL_LIM_WRAPPER_H