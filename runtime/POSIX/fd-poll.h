#ifndef KLEE_FD_POLL_H
#define KLEE_FD_POLL_H

#include <poll.h>
#include <stdbool.h>
#include <pthread.h>

#include "fd.h"

#define KPR_EVENT_CLOSED (1 << 0)
#define KPR_EVENT_READABLE (1 << 1)
#define KPR_EVENT_WRITABLE (1 << 2)
#define KPR_EVENT_READY (1 << 3)
#define KPR_EVENT_ERROR (1 << 4)
#define KPR_EVENT_HUP (1 << 5)

typedef struct {
  int fd;

  int track_event_types;

  // To enable correct tracking we have to add the initial
  // state to the file
  union {
    struct socket {
      int state;
    } socket;
  } initial_state;

  bool closed;
  bool on_notification_list;
} kpr_poll_request_entry;

typedef struct {
  kpr_poll_request_entry* entries;

  size_t entry_count;

  bool has_event;

  pthread_t blocked_thread;
} kpr_poll_request;

void kpr_handle_fd_changed(int fd);

#endif
