#include "klee/runtime/kpr/ringbuffer.h"

#include <stdlib.h>
#include <assert.h>

bool kpr_ringbuffer_create(kpr_ringbuffer* rb, size_t size) {
  if (size == 0) {
    return false;
  }
  
  rb->read_index = 0;
  rb->write_index = 0;

  rb->size = size;
  rb->free_capacity = size;

  rb->buffer = calloc(sizeof(char), size);
  return rb->buffer != NULL;
}

bool kpr_ringbuffer_destroy(kpr_ringbuffer* rb) {
  free(rb->buffer);
  rb->size = 0;
  return true;
}

// Various info calls

bool kpr_ringbuffer_empty(kpr_ringbuffer* rb) {
  return rb->free_capacity == rb->size;
}
bool kpr_ringbuffer_full(kpr_ringbuffer* rb) {
  return rb->free_capacity == 0;
}

bool kpr_ringbuffer_resize(kpr_ringbuffer* rb, size_t new_size) {
  size_t el_count = kpr_ringbuffer_used_size(rb);
  
  if (new_size < el_count) {
    return false;
  }

  char* new_buffer = calloc(sizeof(char), new_size);
  if (new_buffer == NULL) {
    return false;
  }

  char* temp = malloc(sizeof(char) * el_count);
  assert(el_count == kpr_ringbuffer_obtain(rb, temp, el_count));

  free(rb->buffer);

  rb->buffer = new_buffer;
  rb->read_index = 0;
  rb->write_index = 0;
  rb->size = new_size;
  rb->free_capacity = new_size;

  assert(el_count == kpr_ringbuffer_push(rb, temp, el_count));
  free(temp);

  return true;
}

size_t kpr_ringbuffer_size(kpr_ringbuffer* rb) {
  return rb->size;
}

size_t kpr_ringbuffer_used_size(kpr_ringbuffer* rb) {
  return (rb->size - rb->free_capacity);
}

size_t kpr_ringbuffer_unused_size(kpr_ringbuffer* rb) {
  return rb->free_capacity;
}

// Actual data calls

size_t kpr_ringbuffer_obtain(kpr_ringbuffer* rb, char* c, size_t size) {
  size_t i = 0;
  
  while (rb->free_capacity < rb->size && i < size) {
    c[i] = rb->buffer[rb->read_index];
    i++;
    rb->free_capacity++;
    rb->read_index = (rb->read_index + 1) % rb->size;
  }

  return i;
}

size_t kpr_ringbuffer_push(kpr_ringbuffer* rb, const char* c, size_t size) {
  size_t i = 0;
  
    // Write as much as possible
  while (rb->free_capacity > 0 && i < size) {
    rb->buffer[rb->write_index] = c[i];
    i++;
    rb->free_capacity--;
    rb->write_index = (rb->write_index + 1) % rb->size;
  }

  return i;
}
