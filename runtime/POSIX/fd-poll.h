#ifndef KLEE_FD_POLL_H
#define KLEE_FD_POLL_H

#include <poll.h>
#include <stdbool.h>

#include "fd.h"

typedef struct {
  // Original request data
  struct pollfd *fds;
  nfds_t nfd;

  bool *on_notification_list;

  // How many entries changed inside fds -> make sure to count the same entry twice
  nfds_t num_changed;

  pthread_cond_t cond;
} klee_poll_request;

void kpr_handle_fd_notification(klee_poll_request* req, int fd);

#endif