#define _LARGEFILE64_SOURCE
#include "fd.h"
#include "fd-poll.h"

#include "klee/klee.h"
#include "klee/runtime/kpr/list.h"
#include "klee/runtime/kpr/signalling.h"

#include <sys/syscall.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

//
// Internal stuff
//

static int kpr_get_os_events(kpr_poll_request_entry* entry, exe_file_t* file) {
  short events = 0;

  if (entry->track_event_types & KPR_EVENT_HUP) {
    events |= POLLHUP;
  }

  if (entry->track_event_types & KPR_EVENT_READABLE) {
    events |= POLLIN;
  }

  if (entry->track_event_types & KPR_EVENT_WRITABLE) {
    events |= POLLOUT;
  }

  struct pollfd data;
  data.fd = file->fd;
  data.events = events;
  data.revents = 0;

  int result = syscall(__NR_poll, &data, 1, 0);
  if (result < 0) {
    klee_warning("External poll call failed");
    return 0;
  } else if (result == 0) {
    return 0;
  }

  int ret = 0;

  if (data.revents & POLLERR) {
    ret |= KPR_EVENT_ERROR;
  }
  if (data.revents & POLLHUP) {
    ret |= KPR_EVENT_HUP;
  }
  if (data.revents & POLLIN) {
    ret |= KPR_EVENT_WRITABLE;
  }
  if (data.revents & POLLOUT) {
    ret |= KPR_EVENT_READABLE;
  }

  return ret;
}

static int kpr_get_dfile_events(int fd, exe_file_t* file) {
  int events = 0;
  
  if ((file->flags & eReadable) && file->dfile->size > 0) {
    events |= POLLIN;
  }

  if (file->flags & eWriteable) {
    events |= POLLOUT;
  }
  
  return events;
}

static int kpr_get_pipe_events(int fd, exe_file_t* file) {
  exe_pipe_t* p = file->pipe;
  int events = 0;

  if ((file->flags & eReadable) && p->bufSize > p->free_capacity) {
    events |= KPR_EVENT_READABLE;
  }

  if ((file->flags & eWriteable) && p->free_capacity > 0) {
    events |= KPR_EVENT_WRITABLE;
  }

  if (fd == p->readFd) {
    events |= (p->writeFd <= 0) ? KPR_EVENT_HUP : 0;
  }
    
  if (fd == p->writeFd) {
    events |= (p->readFd <= 0) ? KPR_EVENT_HUP : 0;
  }

  return events;
}

static int kpr_get_socket_events(kpr_poll_request_entry* entry, exe_file_t* file) {
  exe_socket_t* socket = file->socket;

  int events = 0;

  int origState = entry->initial_state.socket.state;

  if (origState == EXE_SOCKET_CONNECTING) {
    if (socket->state == EXE_SOCKET_CONNECTED) {
      events |= KPR_EVENT_READY | KPR_EVENT_READABLE;
    }
  } else if (socket->state == EXE_SOCKET_CONNECTED) {
    events |= KPR_EVENT_READY;

    if (socket->readFd >= 0) {
      int e = kpr_get_pipe_events(socket->readFd, __get_file_ignore_flags(socket->readFd));
      events |= (e & (KPR_EVENT_READABLE | KPR_EVENT_HUP | KPR_EVENT_ERROR));
    }

    if (socket->writeFd >= 0) {
      int e = kpr_get_pipe_events(socket->writeFd, __get_file_ignore_flags(socket->writeFd));
      events |= (e & (KPR_EVENT_WRITABLE | KPR_EVENT_HUP | KPR_EVENT_ERROR));
    }
  } else if (socket->state == EXE_SOCKET_PASSIVE) {
    events |= KPR_EVENT_READY;

    if (kpr_list_size(&socket->queued_peers) > 0) {
      events |= KPR_EVENT_READABLE;
    }
  }

  return events;
}

static int kpr_get_events_of(kpr_poll_request_entry* entry, exe_file_t* file) {
  bool closed = (file->flags & eOpen) == 0;

  if (closed) {
    // We do not actually have to remove the
    // req from the notification list as the list
    // is cleared during `close`
    entry->on_notification_list = false;

    // We are overriding this since we do not want to do any
    // further modifications
    entry->closed = true;
    return KPR_EVENT_CLOSED;
  }

  int events = 0;

  if (file->socket) {
    events |= kpr_get_socket_events(entry, file);
  } else if (file->pipe) {
    events |= kpr_get_pipe_events(entry->fd, file);
  } else if (file->dfile) {
    events |= kpr_get_dfile_events(entry->fd, file);
  } else if (file->fd >= 0) {
    events |= kpr_get_os_events(entry, file);
  }

  return (events & entry->track_event_types);
}

static void kpr_handle_file_changed(kpr_poll_request* r, int fd, exe_file_t* file) {
  bool had_event = r->has_event;
  if (had_event) {
    // We already have an event and notified the thread
    return;
  }
  
  for (size_t i = 0; i < r->entry_count; i++) {
    kpr_poll_request_entry* entry = &r->entries[i];

    if (entry->fd != fd) {
      continue;
    }

    if (entry->closed) {
      // A fd can be reused, so make sure that we do not make
      // any modifications
      continue;
    }

    int events = kpr_get_events_of(entry, file);
    if (events != 0) {
      r->has_event = true;
      break;
    }
  }

  if (!had_event && r->has_event) {
    assert(r->blocked_thread != NULL);
    kpr_signal_thread(r->blocked_thread);
  }
}

void kpr_handle_fd_changed(int fd) {
  exe_file_t* file = __get_file_ignore_flags(fd);
  assert(file != NULL);

  kpr_list_iterator it = kpr_list_iterate(&file->notification_list);
  while(kpr_list_iterator_valid(it)) {
    kpr_poll_request* r = kpr_list_iterator_value(it);

    kpr_handle_file_changed(r, fd, file);

    kpr_list_iterator_next(&it);
  }
}

//
// External api (mainly only poll for now)
//


int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
  if (nfds == 0) {
    return 0;
  } 

  klee_check_memory_access(fds, sizeof(struct pollfd) * nfds);

  kpr_poll_request req;
  req.entry_count = nfds;
  req.entries = calloc(sizeof(kpr_poll_request_entry), nfds);
  req.has_event = false;

  pthread_mutex_lock(klee_fs_lock());

  size_t changes = 0;

  // First transform to our req structure
  for (size_t i = 0; i < nfds; i++) {
    struct pollfd* d = &fds[i];
    kpr_poll_request_entry* e = &req.entries[i];

    if (d->fd < 0) {
      e->fd = -1;
      continue;
    }

    d->revents = 0;

    exe_file_t* file = __get_file(d->fd);
    if (file == NULL) {
      fds[i].revents = POLLNVAL;
      changes++;
      continue;
    }

    if (file->socket && (file->socket->state == EXE_SOCKET_INIT && file->socket->state == EXE_SOCKET_BOUND)) {
      fds[i].revents = POLLNVAL;
      changes++;
      continue;
    }

    e->fd = d->fd;
    e->track_event_types = KPR_EVENT_ERROR | KPR_EVENT_READY | KPR_EVENT_CLOSED;
    
    if (d->events & POLLIN) {
      e->track_event_types |= KPR_EVENT_READABLE;
    }

    if (d->events & POLLOUT) {
      e->track_event_types |= KPR_EVENT_WRITABLE;
    }

    if (d->events & POLLHUP) {
      e->track_event_types |= KPR_EVENT_WRITABLE;
    }

    // Copy in initial state
    if (file->socket) {
      e->initial_state.socket.state = file->socket->state;
    }
  }

  if (changes > 0) {
    free(req.entries);

    pthread_mutex_unlock(klee_fs_lock());

    return changes;
  }

  // ...

  while (1) {
    req.has_event = false;
    changes = 0;

    for (size_t i = 0; i < req.entry_count; i++) {
      kpr_poll_request_entry* entry = &req.entries[i];
      struct pollfd* pfd = &fds[i];

      if (entry->fd < 0) {
        continue;
      }

      if (entry->closed) {
        pfd->revents = POLLNVAL;
        changes++;
        req.has_event = true;
        continue;
      }

      exe_file_t* file = __get_file(entry->fd);
      int events = kpr_get_events_of(entry, file);
      
      if (events & KPR_EVENT_HUP) {
        pfd->revents |= POLLHUP;
      }
      if (events & KPR_EVENT_ERROR) {
        pfd->revents |= POLLERR;
      }
      if (events & KPR_EVENT_READABLE) {
        pfd->revents |= POLLIN;
      }
      if (events & KPR_EVENT_WRITABLE) {
        pfd->revents |= POLLOUT;
      }
      
      if (pfd->revents != 0) {
        changes++;
        req.has_event = true;
      }
    }

    if (req.has_event) {
      break;
    }

    if (timeout == 0) {
      free(req.entries);

      pthread_mutex_unlock(klee_fs_lock());
      return 0;
    }

    // We have to sleep since until we get new events

    for (size_t i = 0; i < req.entry_count; i++) {
      kpr_poll_request_entry* entry = &req.entries[i];

      if (entry->closed || entry->fd < 0) {
        continue;
      }

      if (!entry->on_notification_list) {
        exe_file_t* file = __get_file(entry->fd);
        kpr_list_push(&file->notification_list, &req);
        entry->on_notification_list = true;
      }
    }

    req.blocked_thread = pthread_self();
    kpr_wait_thread_self(klee_fs_lock());
    req.blocked_thread = NULL;
  }

  // Make sure that we clear all entries in notification lists
  for (size_t i = 0; i < req.entry_count; i++) {
    kpr_poll_request_entry* entry = &req.entries[i];

    if (entry->closed || entry->fd < 0 || !entry->on_notification_list) {
      continue;
    }

    exe_file_t* file = __get_file(entry->fd);
    kpr_list_remove(&file->notification_list, &req);
  }

  free(req.entries);

  pthread_mutex_unlock(klee_fs_lock());

  return changes;
}
