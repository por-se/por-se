#include "out.h"

#include "klee/klee.h"

#include <string.h>

static void* get_concrete_ptr(const void *p) {
  void* res;

  if (sizeof(p) == 4) {
    res = (void*) klee_get_value_i32(p);
  } else if (sizeof(p) == 8) {
    res = (void*) klee_get_value_i64(p);
  }

  return res;
}

static size_t get_concrete_size(size_t s) {
  size_t res;

  if (sizeof(s) == 4) {
    res = (size_t) klee_get_value_i32(s);
  } else if (sizeof(s) == 8) {
    res = (size_t) klee_get_value_i64(s);
  }

  return res;
}

static char get_concrete_char(char c) {
  return (char) klee_get_value_i32((int32_t) c);
}

ssize_t kpr_output(int target, const char* _b, size_t _c) {
  if (target != KPR_OUTPUT_STDOUT && target != KPR_OUTPUT_STDERR) {
    return -1;
  }

  const char* buffer = get_concrete_ptr(_b);
  size_t count = get_concrete_size(_c);

  // Make sure that the rest of the program also
  // uses the same chosen values
  klee_assume(buffer == _b);
  klee_assume(count == _c);

  // Ensure that we can access the buffer
  klee_check_memory_access(buffer, count);

  char* out_buffer = (char*) malloc(count);
  if (out_buffer == NULL) {
    return -1;
  }
  
  for (size_t i = 0; i < count; i++) {
    out_buffer[i] = get_concrete_char(buffer[i]);
  }

  klee_output(target, out_buffer);

  free(out_buffer);
  return count;
}

static size_t string_length(const char* str) {
  size_t i = 0;
  while (str[i] != '\0') i++;
  return i;
}

/*
 * POSIX output functions we provide as built-in
 */

int puts(const char* out) {
  size_t len = string_length(out);

  char* b = malloc(len + 1);
  memcpy(b, out, len);

  b[len] = '\n';

  kpr_output(KPR_OUTPUT_STDOUT, b, len + 1);

  free(b);

  return 0;
}
