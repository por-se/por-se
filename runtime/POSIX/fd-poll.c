#define _LARGEFILE64_SOURCE
#include "fd.h"
#include "fd-poll.h"

#include "klee/klee.h"
#include "klee/runtime/kpr/list.h"

#include <sys/syscall.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

static int check_poll_flags(short event, short *revent, exe_file_t* file) {
  short ret = 0;

  if (file->pipe) {
    exe_pipe_t* p = file->pipe;

    if ((event & POLLIN) && (file->flags & eReadable) && p->bufSize > p->free_capacity) {
      ret |= POLLIN;
    }

    if ((event & POLLOUT) && (file->flags & eWriteable) && p->free_capacity > 0) {
      ret |= POLLOUT;
    }

    if ((event & POLLHUP) && (file->flags & eOpen) == 0) {
      ret |= POLLHUP;
    }
  } else if (file->dfile) {
    if ((event & POLLIN) && (file->flags & eReadable)) {
      ret |= POLLIN;
    }

    if ((event & POLLOUT) && (file->flags & eWriteable)) {
      ret |= POLLIN;
    }
  } else if (file->fd >= 0) {
    struct pollfd data;
    data.fd = file->fd;
    data.events = event;
    data.revents = 0;

    int result = syscall(__NR_poll, &data, 1, 0);
    if (result == 1) {
      ret |= data.revents;
    } else if (result < 0) {
      return result;
    }
  } else {
    return -1;
  }

  *revent = ret;

  return 0;
}

static struct pollfd* get_pollfd_via_fd(klee_poll_request* req, int fd, struct pollfd* skip) {
  for (nfds_t i = 0; i < req->nfd; i++) {
    struct pollfd* cur = &(req->fds[i]);

    if (cur->fd == fd && cur != skip) {
      return cur;
    }
  }

  return NULL;
}

static void remove_from_notification_list(klee_poll_request* req, exe_file_t* file) {
  kpr_list_iterator it = kpr_list_iterate(&file->notification_list);
  while(kpr_list_iterator_valid(it)) {
    klee_poll_request* r = kpr_list_iterator_value(it);

    if (req == r) {
      kpr_list_erase(&file->notification_list, &it);
    }

    kpr_list_iterator_next(&it);
  }

  for (nfds_t i = 0; i < req->nfd; i++) {
    if (__get_file_ignore_flags(req->fds[i].fd) == file) {
      req->on_notification_list[i] = false;
    }
  }
}

static nfds_t kpr_check_fd(klee_poll_request* req, int fd) {
  exe_file_t* file = __get_file(fd);
  assert(file != NULL);

  nfds_t updateCount = 0;

  struct pollfd* cur = NULL;
  while (1) {
    cur = get_pollfd_via_fd(req, fd, cur);

    if (cur == NULL) {
      break;
    }

    short revent = 0;
    if (check_poll_flags(cur->events, &revent, file) != 0) {
      continue;
    }

    if (revent != 0) {
      updateCount++;

      if ((file->flags & eOpen) == 0) {
        // So another mayor problem: the underlying file closed.
        // This is a valid case, but we have to make sure that:
        // * that we are no longer on the notification list
        // * we no longer refer to that fd in the list
        remove_from_notification_list(req, file);
      }

      if (cur->revents == 0) {
        req->num_changed++;
      }

      cur->revents |= revent;
    }
  }

  return updateCount;
}

void kpr_handle_fd_notification(klee_poll_request* req, int fd) {
  nfds_t updateCount = kpr_check_fd(req, fd);

  if (updateCount > 0) {
    pthread_cond_signal(&req->cond);
  }
}

int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
  if (nfds == 0) {
    return 0;
  } 

  klee_check_memory_access(fds, sizeof(struct pollfd) * nfds);

  pthread_mutex_lock(klee_fs_lock());

  // Welp we need to temporarily create a binding between all
  // requested fds and the actual underlying objects (pipe, disk_file)

  klee_poll_request req;
  req.fds = fds;
  req.nfd = nfds;
  req.num_changed = 0;

  // First make sure that the return set is initialized correctly aka everything is zero
  for (nfds_t i = 0; i < nfds; i++) {
    struct pollfd* cur = &(req.fds[i]);
    cur->revents = 0;

    if (cur->fd < 0) {
      continue;
    }

    exe_file_t* file = __get_file(cur->fd);
    if (file == NULL) {
      pthread_mutex_unlock(klee_fs_lock());
      return -1;
    }

    int check_ret = check_poll_flags(cur->events, &cur->revents, file);

    if (check_ret < 0) {
      pthread_mutex_unlock(klee_fs_lock());
      return check_ret;
    }

    if (cur->revents != 0) {
      req.num_changed++;
    }
  }

  if (req.num_changed > 0 || timeout == 0) {
    // So we already have a change -> make sure that we
    // return the data now
    pthread_mutex_unlock(klee_fs_lock());
    return req.num_changed;
  }

  // We have to actually wait for something to happen now. Therefore
  // add the req to the files
  pthread_cond_init(&req.cond, NULL);

  req.on_notification_list = calloc(1, sizeof(bool) * nfds);

  for (nfds_t k = 0; k < nfds; k++) {
    struct pollfd* cur = &(req.fds[k]);
    if (cur->fd < 0) {
      req.on_notification_list[k] = false;
      continue;
    }

    exe_file_t* file = __get_file(cur->fd);
    kpr_list_push(&file->notification_list, &req);
    req.on_notification_list[k] = true;
  }

  while (req.num_changed == 0) {
    pthread_cond_wait(&req.cond, klee_fs_lock());
  }

  // Remove request from all files
  for (nfds_t j = 0; j < nfds; j++) {
    struct pollfd* cur = &(req.fds[j]);
    if (cur->fd < 0 || !req.on_notification_list[j]) {
      continue;
    }

    exe_file_t* file = __get_file(cur->fd);
    remove_from_notification_list(&req, file);
  }

  assert(pthread_cond_destroy(&req.cond) == 0);
  free(req.on_notification_list);

  pthread_mutex_unlock(klee_fs_lock());
  return req.num_changed;
}