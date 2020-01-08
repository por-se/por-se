#define _LARGEFILE64_SOURCE
#include "fd.h"
#include "fd-poll.h"
#include "runtime-lock.h"

#include "klee/klee.h"
#include "klee/runtime/kpr/list.h"
#include "klee/runtime/kpr/signalling.h"

#include <sys/syscall.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

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

  if ((file->flags & eReadable) && !kpr_ringbuffer_empty(&p->buffer)) {
    events |= KPR_EVENT_READABLE;
  }

  if ((file->flags & eWriteable) && !kpr_ringbuffer_full(&p->buffer)) {
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

    struct kpr_tcp* tcp = &socket->proto.tcp;

    if (tcp->peer == NULL) {
      events |= KPR_EVENT_HUP;
    } else {
      struct kpr_tcp* p_tcp = &tcp->peer->proto.tcp;

      if (!kpr_ringbuffer_full(&p_tcp->buffer)) {
        events |= KPR_EVENT_WRITABLE;
      }
    }

    if (!kpr_ringbuffer_empty(&tcp->buffer)) {
      events |= KPR_EVENT_READABLE;
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
// External api (POSIX API)
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

  kpr_acquire_runtime_lock();

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

    kpr_release_runtime_lock();

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

      kpr_release_runtime_lock();
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
    kpr_wait_thread_self(kpr_runtime_lock());
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

  kpr_release_runtime_lock();

  return changes;
}

#undef FD_SET
#undef FD_CLR
#undef FD_ISSET
#undef FD_ZERO
#define	FD_SET(n, p)	((p)->fds_bits[(n)/NFDBITS] |= (1 << ((n) % NFDBITS)))
#define	FD_CLR(n, p)	((p)->fds_bits[(n)/NFDBITS] &= ~(1 << ((n) % NFDBITS)))
#define	FD_ISSET(n, p)	((p)->fds_bits[(n)/NFDBITS] & (1 << ((n) % NFDBITS)))
#define FD_ZERO(p)	memset((char *)(p), '\0', sizeof(*(p)))
int select(int nfds, fd_set *read, fd_set *write,
           fd_set *except, struct timeval *timeout) {
  kpr_acquire_runtime_lock();

  fd_set in_read, in_write, in_except;

  if (read) {
    in_read = *read;
    FD_ZERO(read);
  } else {
    FD_ZERO(&in_read);
  }

  if (write) {
    in_write = *write;
    FD_ZERO(write);
  } else {
    FD_ZERO(&in_write);
  }
   
  if (except) {
    in_except = *except;
    FD_ZERO(except);
  } else {
    FD_ZERO(&in_except);
  }

  struct pollfd* poll_fds = calloc(sizeof(struct pollfd), nfds);

  for (int i = 0; i < nfds; i++) {
    struct pollfd* pfd = &poll_fds[i];
    
    if (!FD_ISSET(i, &in_read) && !FD_ISSET(i, &in_write) && !FD_ISSET(i, &in_except)) {
      pfd->fd = -1;
      continue;
    }

    exe_file_t *f = __get_file(i);
    if (!f) {
      free(poll_fds);
      errno = EBADF;
      kpr_release_runtime_lock();
      return -1;
    }

    pfd->fd = i;

    if (FD_ISSET(i, &in_read)) {
      pfd->events |= POLLERR | POLLIN | POLLHUP;
    }

    if (FD_ISSET(i, &in_write)) {
      pfd->events |= POLLERR | POLLOUT;
    }

    if (FD_ISSET(i, &in_except)) {
      pfd->events |= POLLERR;
    }
  }

  int poll_timeout = -1;
  if (timeout != NULL) {
    // Round slightly up for microseconds as poll uses milliseconds
    poll_timeout = (timeout->tv_usec + 999) / 1000;
    poll_timeout += timeout->tv_sec * 1000;
  }

  int ret = poll(poll_fds, nfds, poll_timeout);

  if (ret <= 0) {
    free(poll_fds);
    kpr_release_runtime_lock();
    return ret;
  }

  int count_fds = 0;

  for (int i = 0; i < nfds; i++) {
    struct pollfd* pfd = &poll_fds[i];
    
    if (pfd->revents == 0) {
      continue;
    }

    assert(FD_ISSET(i, &in_read) || FD_ISSET(i, &in_write) || FD_ISSET(i, &in_except));

    if (FD_ISSET(i, &in_read) && (pfd->revents & (POLLERR | POLLIN | POLLHUP)) != 0) {
      FD_SET(i, read);
    }

    if (FD_ISSET(i, &in_write) && (pfd->revents & (POLLERR | POLLOUT)) != 0) {
      FD_SET(i, write);
    }

    if (FD_ISSET(i, &in_except) && (pfd->revents & POLLERR) != 0) {
      FD_SET(i, except);
    }

    count_fds++;
  }

  free(poll_fds);

  kpr_release_runtime_lock();
  return count_fds;
}
