#include <stdbool.h>

#include "klee/klee.h"
#include "klee/runtime/pthread.h"

#include "../kpr/internal.h"

static int detect_init_pattern(pthread_internal_t* obj, size_t size) {
  klee_check_memory_access(obj, size);

  if (obj->magic == PTHREAD_INTERNAL_MAGIC_VALUE) {
    return KPR_OTHER_INIT;
  }

  const char* data = (const char*) obj;

  bool onlyRandom = true;
  bool onlyZero = true;

  size_t i;
  for (i = 0; i < size; i++) {
    const char d = data[i];

    // Klee initializes the objects with a pattern of 0xAB for random memory
    onlyRandom &= (d == (char) 0xAB);
    onlyZero &= (d == 0);
  }

  if (onlyRandom) {
    return KPR_RANDOM_INIT;
  }

  if (onlyZero) {
    return KPR_ZERO_INIT;
  }

  return KPR_INVALID;
}

void kpr_check_init_pattern(pthread_internal_t* obj, size_t size) {
  int pattern = detect_init_pattern(obj, size);

  if (pattern == KPR_INVALID) {
    klee_report_error(__FILE__, __LINE__, "Trying to use an invalid object as a pthread object", "user");
  }

  if (pattern == KPR_RANDOM_INIT) {
    klee_report_error(__FILE__, __LINE__, "Trying to use an uninitialized pthread object", "user");
  }

  if (pattern == KPR_ZERO_INIT) {
    klee_warning("Using a zero-initialized pthread object is often supported, but can also trigger undefined behavior");
    klee_stack_trace();
    obj->magic = PTHREAD_INTERNAL_MAGIC_VALUE;
  }
}
