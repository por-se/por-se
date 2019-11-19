#ifndef KPR_RINGBUFFER_H
#define KPR_RINGBUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
  size_t size;
  size_t free_capacity;

  char* buffer;

  size_t read_index;
  size_t write_index;
} kpr_ringbuffer;

bool kpr_ringbuffer_create(kpr_ringbuffer* rb, size_t size);

bool kpr_ringbuffer_empty(kpr_ringbuffer* rb);
bool kpr_ringbuffer_full(kpr_ringbuffer* rb);

size_t kpr_ringbuffer_size(kpr_ringbuffer* rb);
size_t kpr_ringbuffer_used_size(kpr_ringbuffer* rb);
size_t kpr_ringbuffer_unused_size(kpr_ringbuffer* rb);

bool kpr_ringbuffer_resize(kpr_ringbuffer* rb, size_t new_size);

size_t kpr_ringbuffer_obtain(kpr_ringbuffer* rb, char* c, size_t size);
size_t kpr_ringbuffer_push(kpr_ringbuffer* rb, const char* c, size_t size);

void kpr_ringbuffer_clear(kpr_ringbuffer* rb);

bool kpr_ringbuffer_destroy(kpr_ringbuffer* rb);

#endif // KPR_RING_BUFFER_H
