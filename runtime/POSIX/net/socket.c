#include "klee/klee.h"
#include "klee/runtime/kpr/list.h"
#include "klee/runtime/kpr/signalling.h"

#include "../fd.h"
#include "../fd-poll.h"
#include "socket.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define min(x,y) ((x) < (y) ? (x) : (y))

static kpr_list open_sockets = KPR_LIST_INITIALIZER;
static kpr_list open_unix_sockets = KPR_LIST_INITIALIZER;
static kpr_list waiting_sockets = KPR_LIST_INITIALIZER;

static exe_socket_t* get_socket_by_port(int port) {
  kpr_list_iterator it = kpr_list_iterate(&open_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket->domain == AF_INET && socket->opened.port == port) {
      return socket;
    }

    kpr_list_iterator_next(&it);
  }

  return NULL;
}

static exe_socket_t* get_socket_by_unix(const char* path) {
  kpr_list_iterator it = kpr_list_iterate(&open_unix_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket->domain == AF_UNIX && strcmp(socket->opened.path, path) == 0) {
      return socket;
    }

    kpr_list_iterator_next(&it);
  }

  return NULL;
}

static int create_socket(exe_socket_t** target) {
  int fd = __get_unused_fd();
  if (fd < 0) {
    errno = EMFILE;
    return -1;
  }

  exe_file_t* f = __get_file_ignore_flags(fd);
  f->flags = (eOpen | eWriteable | eReadable);
  kpr_list_create(&f->notification_list);

  exe_socket_t* socket = calloc(sizeof(exe_socket_t), 1);
  socket->writeFd = -1;
  socket->readFd = -1;
  socket->state = EXE_SOCKET_INIT;
  kpr_list_create(&socket->blocked_threads);

  f->socket = socket;
  socket->own_fd = fd;

  if (target) {
    *target = socket;
  }

  return fd;
}

static exe_socket_t* find_waiting_by_req_port(int port) {
  kpr_list_iterator it = kpr_list_iterate(&waiting_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket->domain == AF_INET && socket->requested.port == port) {
      kpr_list_erase(&waiting_sockets, &it);

      assert(socket->state == EXE_SOCKET_CONNECTING);
      return socket;
    }

    kpr_list_iterator_next(&it);
  }

  return NULL;
}

static exe_socket_t* find_waiting_by_unix_path(const char* path) {
  kpr_list_iterator it = kpr_list_iterate(&waiting_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket->domain == AF_UNIX && strcmp(socket->requested.path, path) == 0) {
      kpr_list_erase(&waiting_sockets, &it);

      assert(socket->state == EXE_SOCKET_CONNECTING);
      return socket;
    }

    kpr_list_iterator_next(&it);
  }

  return NULL;
}

static void check_for_fake_packets(exe_socket_t* socket) {
  assert(socket->state == EXE_SOCKET_PASSIVE);

  kpr_list_iterator it = kpr_list_iterate(&__exe_env.fake_packets);
  while(kpr_list_iterator_valid(it)) {
    exe_fake_packet_t* faked_packet = kpr_list_iterator_value(it);

    if (socket->domain == AF_INET && socket->opened.port == faked_packet->port) {
      exe_socket_t* symSocket;
      int fd = create_socket(&symSocket);

      if (fd < 0) {
        klee_warning("could not create socket for sym port - aborting");
        return;
      }

      symSocket->state = EXE_SOCKET_CONNECTING;
      symSocket->requested.port = faked_packet->port;
      symSocket->readFd = -1;
      symSocket->writeFd = -1;
      symSocket->faked_packet = faked_packet;
      symSocket->domain = AF_INET;

      kpr_list_push(&socket->queued_peers, symSocket);

      // Only remove once we set up the requesting peer
      kpr_list_erase(&__exe_env.fake_packets, &it);
    }

    kpr_list_iterator_next(&it);
  }
}

int socket(int domain, int typeAndFlags, int protocol) {
  // TODO: handle protocol a bit better

  if (domain != AF_INET && domain != AF_UNIX) {
    klee_warning("socket request with unsupported domain");
    errno = ENOMEM;
    return -1;
  }

  int type = typeAndFlags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  int flags = typeAndFlags & (SOCK_NONBLOCK | SOCK_CLOEXEC);

  if (type != SOCK_STREAM && type != SOCK_DGRAM) {
    klee_warning("socket request with unsupported type");
    errno = ENOMEM;
    return -1;
  }

  pthread_mutex_lock(klee_fs_lock());

  exe_socket_t* socket;
  int fd = create_socket(&socket);
  exe_file_t* f = __get_file(fd);
  
  if (flags & SOCK_NONBLOCK) {
    f->flags |= eNonBlock;
  }
  if (flags & SOCK_CLOEXEC) {
    f->flags |= eCloseOnExec;
  }

  socket->type = type;
  socket->domain = domain;

  pthread_mutex_unlock(klee_fs_lock());
  return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  pthread_mutex_lock(klee_fs_lock());
  exe_file_t* f = __get_file(sockfd);
  if (!f->socket) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EBADF;
    return -1;
  }

  if (f->socket->state != EXE_SOCKET_INIT) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EINVAL;
    return -1;
  }

  int domain = f->socket->domain;
  if (addr->sa_family != domain) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EINVAL;
    return -1;
  }

  size_t req_size = domain == AF_INET 
    ? sizeof(struct sockaddr_in)
    : sizeof(struct sockaddr_un);

  if (req_size > addrlen) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EINVAL;
    return -1;
  }

  f->socket->saddress_len = req_size;
  f->socket->saddress = malloc(req_size);

  memcpy(f->socket->saddress, addr, req_size);

  f->socket->state = EXE_SOCKET_BOUND;

  pthread_mutex_unlock(klee_fs_lock());
  return 0;
}

static bool open_to_local_env(exe_socket_t* socket) {
  if (socket->domain == AF_INET) {
    int port = 0;

    assert(socket->opened.port == 0);

    if (socket->saddress) {
      struct sockaddr_in* addr = (struct sockaddr_in*) socket->saddress;
      port = ntohs(addr->sin_port);

      assert(get_socket_by_port(port) == NULL);
    } else {
      // Try to find the lowest not yet assigned port number
      for (int i = 49152; i <= 65535; i++) {
        if (get_socket_by_port(i) == NULL) {
          port = i;
          break;
        }
      }

      if (port == 0) {
        klee_warning("Used up all port numbers? Should be impossible");
        return false;
      }

      // Now we actually have to create the saddress
      socket->saddress_len = sizeof(struct sockaddr_in);
      struct sockaddr_in* addr = (struct sockaddr_in*) calloc(socket->saddress_len, 1);
      socket->saddress = (struct sockaddr*) addr;

      addr->sin_family = AF_INET;
      addr->sin_port = htons(port);

      // (127 << 24) | 1 ~~~> 127.0.0.1
      addr->sin_addr.s_addr = htonl((127 << 24) | 1);
    }
    
    assert(port != 0);
    socket->opened.port = port;

    // Adding it to the list of known sockets
    kpr_list_push(&open_sockets, socket);
  } else if (socket->domain == AF_UNIX) {
    struct sockaddr_un* addr;

    if (socket->saddress) {
      addr = (struct sockaddr_un*) socket->saddress;
    } else {
      socket->saddress_len = sizeof(struct sockaddr_un);
      addr = (struct sockaddr_un*) calloc(socket->saddress_len, 1);
      socket->saddress = (struct sockaddr*) addr;

      addr->sun_family = AF_UNIX;
      addr->sun_path[0] = '\0';
    }

    socket->opened.path = addr->sun_path;

    if (socket->opened.path[0] != '\0') {
      kpr_list_push(&open_unix_sockets, socket);
    }
  } else {
    assert(0);
  }

  return true;
}

int listen(int sockfd, int backlog) {
  pthread_mutex_lock(klee_fs_lock());
  exe_file_t* f = __get_file(sockfd);

  exe_socket_t* s = f->socket;

  if (!s) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = ENOTSOCK;
    return -1;
  }

  if (s->state != EXE_SOCKET_BOUND) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EOPNOTSUPP;
    return -1;
  }

  if (s->domain == AF_INET && get_socket_by_port(s->requested.port) != NULL) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EADDRINUSE;
    return -1;
  }

  // TODO: how to properly handle 'anonymous paths'
  if (s->domain == AF_UNIX && get_socket_by_unix(s->requested.path) != NULL) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EADDRINUSE;
    return -1;
  }

  s->state = EXE_SOCKET_PASSIVE;

  open_to_local_env(s);

  check_for_fake_packets(s);

  pthread_mutex_unlock(klee_fs_lock());
  return 0;
}

static bool copy_socket_addr_into(exe_socket_t* socket, struct sockaddr* addr, socklen_t* len) {
  assert(socket->saddress != NULL);

  int copy = min(socket->saddress_len, *len);
  memcpy(addr, socket->saddress, copy);

  if (socket->saddress_len > *len) {
    *len = socket->saddress_len;
  }

  return true;
}

static int establish(exe_socket_t* passive, exe_socket_t* connecting) {
  // Since sockets communicate in two directions, we use two pipes
  int flags = O_NONBLOCK;

  int pipeOne[2];
  if (pipe2(pipeOne, flags) < 0) {
    return -1;
  }

  int pipeTwo[2];
  if (pipe2(pipeTwo, flags) < 0) {
    close(pipeOne[0]);
    close(pipeOne[1]);
    return -1;
  }

  // TODO: refactor to support udp / tcp more easily
  passive->readFd = pipeOne[0];
  passive->writeFd = pipeOne[1];

  connecting->readFd = pipeTwo[0];
  connecting->writeFd = pipeTwo[1];

  // Now mark them also as connected
  connecting->state = EXE_SOCKET_CONNECTED;
  passive->state = EXE_SOCKET_CONNECTED;

  connecting->peer = passive;
  passive->peer = connecting;

  if (!open_to_local_env(connecting)) {
    close(pipeOne[0]);
    close(pipeOne[1]);

    close(pipeTwo[0]);
    close(pipeTwo[1]);

    return -1;
  }

  if (!open_to_local_env(passive)) {
    close(pipeOne[0]);
    close(pipeOne[1]);

    close(connecting->own_fd);

    return -1;
  }

  return 0;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  pthread_mutex_lock(klee_fs_lock());
  exe_file_t* f = __get_file(sockfd);

  if (!f->socket) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = ENOTSOCK;
    return -1;
  }

  if (f->socket->state != EXE_SOCKET_PASSIVE) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = ENOTSOCK;
    return -1;
  }

  exe_socket_t* peer = NULL;
  while (1) {
    if (kpr_list_size(&f->socket->queued_peers) > 0) {
      peer = kpr_list_pop(&f->socket->queued_peers);
      assert(peer->state == EXE_SOCKET_CONNECTING);
      break;
    }

    if (f->socket->domain == AF_INET) {
      peer = find_waiting_by_req_port(f->socket->opened.port);

      if (peer != NULL) {
        assert(peer->state == EXE_SOCKET_CONNECTING);
        break;
      }
    }

    if (f->socket->domain == AF_INET) {
      peer = find_waiting_by_unix_path(f->socket->opened.path);

      if (peer != NULL) {
        assert(peer->state == EXE_SOCKET_CONNECTING);
        break;
      }
    }

    if (f->flags & eNonBlock) {
      pthread_mutex_unlock(klee_fs_lock());
      errno = EWOULDBLOCK;
      return -1;
    }

    kpr_list_push(&f->socket->blocked_threads, pthread_self());
    kpr_wait_thread_self(klee_fs_lock());
  }

  // Now we have to create yet another socket that is used for the actual communication
  exe_socket_t* newSocket;
  int newSocketFd = create_socket(&newSocket);
  if (newSocketFd < 0) {
    kpr_list_push(&f->socket->queued_peers, peer);
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  newSocket->domain = f->socket->domain;
  newSocket->type = f->socket->type;

  if (establish(newSocket, peer) < 0) {
    kpr_list_push(&f->socket->queued_peers, peer);
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (f->flags & eNonBlock) {
    __get_file(newSocketFd)->flags |= eNonBlock;
  }

  if (addr != NULL) {
    copy_socket_addr_into(peer, addr, addrlen);
  }

  // And wake up the peer
  if (peer->faked_packet == NULL) {
    notify_thread_list(&peer->blocked_threads);

    kpr_handle_fd_changed(peer->own_fd);
  } else {
    // If we write too many bytes, then we risk blocking this
    assert(peer->faked_packet->packet_length <= PIPE_BUFFER_SIZE);
    ssize_t ctn = write(newSocket->writeFd, peer->faked_packet->data, peer->faked_packet->packet_length);
    if (ctn < 0) {
      klee_warning("Failed to write the symbolic data");
    } else if (ctn != peer->faked_packet->packet_length) {
      klee_warning("Failed to write all symbolic data - only parts");
    }

    close(peer->readFd);
    close(peer->writeFd);

    peer->readFd = -1;
    peer->writeFd = -1;

    peer->type = f->socket->type;
  }

  pthread_mutex_unlock(klee_fs_lock());
  return newSocketFd;
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
  pthread_mutex_lock(klee_fs_lock());

  int ret = accept(sockfd, addr, addrlen);
  if (ret >= 0) {
    exe_file_t* file = __get_file(ret);
    
    if (flags & SOCK_NONBLOCK) {
      file->flags |= eNonBlock;
    }

    if (flags & SOCK_CLOEXEC) {
      file->flags |= eCloseOnExec;
    }
  }

  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* f = __get_file(sockfd);
  if (!f->socket) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EBADF;
    return -1;
  }

  if (f->socket->state != EXE_SOCKET_INIT) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EISCONN;
    return -1;
  }

  if (addrlen < sizeof(struct sockaddr_in)) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EINVAL;
    return -1;
  }

  if (f->socket->domain == AF_INET) {
    int requestedPort = ntohs(((struct sockaddr_in*) addr)->sin_port);
    f->socket->requested.port = requestedPort;
  } else if (f->socket->domain == AF_UNIX) {
    struct sockaddr_un* as_unix = (struct sockaddr_un*) addr;
    char* path = as_unix->sun_path;
    
    size_t size = sizeof(as_unix->sun_path);
    char* copy = (char*) calloc(size, 1);
    memcpy(copy, path, size);

    f->socket->requested.path = copy;
  } else {
    assert(0 && "Unsupported");
  }

  f->socket->state = EXE_SOCKET_CONNECTING;
  f->socket->readFd = -1;
  f->socket->writeFd = -1;

  bool inWaitingList = false;
  exe_socket_t* socket = NULL;

  // Block until we know that the socket exists 
  while (1) {
    if (f->socket->domain == AF_INET) {
      socket = get_socket_by_port(f->socket->requested.port);
    } else if (f->socket->domain == AF_UNIX) {
      socket = get_socket_by_unix(f->socket->requested.path);
    }

    if (socket) {
      if (socket->state != EXE_SOCKET_PASSIVE) {
        if (inWaitingList) {
          kpr_list_remove(&waiting_sockets, f->socket);
        }

        pthread_mutex_unlock(klee_fs_lock());
        errno = ECONNREFUSED;
        return -1;
      } else {
        if (inWaitingList) {
          kpr_list_remove(&waiting_sockets, f->socket);
        }

        break;
      }
    }

    if (!inWaitingList) {
      kpr_list_push(&waiting_sockets, f->socket);
      inWaitingList = true;
    }

    kpr_list_push(&f->socket->blocked_threads, pthread_self());
    kpr_wait_thread_self(klee_fs_lock());
  }

  assert(socket != NULL);
  if (inWaitingList) {
    kpr_list_remove(&waiting_sockets, f->socket);
  }

  // Now we add ourselfs to the waiting list of the socket
  inWaitingList = false;
  while (f->socket->peer == NULL) {
    // Connection was not established, but we now know that there
    // is a socket waiting
    if (!inWaitingList) {
      kpr_list_push(&socket->queued_peers, f->socket);
      inWaitingList = true;
    }

    notify_thread_list(&socket->blocked_threads);
    kpr_handle_fd_changed(socket->own_fd);

    if (f->flags & eNonBlock) {
      pthread_mutex_unlock(klee_fs_lock());

      errno = EINPROGRESS;
      return -1;
    } else {
      kpr_list_push(&f->socket->blocked_threads, pthread_self());
      kpr_wait_thread_self(klee_fs_lock());
    }
  }

  pthread_mutex_unlock(klee_fs_lock());
  return 0;
}

int kpr_close_socket(exe_file_t* file) {
  assert(file->socket != NULL);

  if (file->socket->writeFd != -1) {
    close(file->socket->writeFd);
    file->socket->writeFd = -1;
  }

  if (file->socket->readFd != -1) {
    close(file->socket->readFd);
    file->socket->readFd = -1;
  }

  kpr_list_remove(&open_sockets, file->socket);

  if (file->socket->saddress) {
    free(file->socket->saddress);
    file->socket->saddress = NULL;
  }

  kpr_handle_fd_changed(file->socket->own_fd);

  if (file->socket->peer) {
    kpr_handle_fd_changed(file->socket->peer->own_fd);

    assert(file->socket->peer->peer == file->socket);
    file->socket->peer->peer = NULL;
  }

  if (file->socket->domain == AF_UNIX) {
    if (file->socket->state == EXE_SOCKET_CONNECTING || file->socket->state == EXE_SOCKET_CONNECTED) {
      free(file->socket->requested.path);
    }
  }

  free(file->socket);
  file->socket = NULL;

  return 0;
}

int shutdown(int sockfd, int how) {
  if (how != SHUT_RDWR && how != SHUT_RD && how != SHUT_WR) {
    return EINVAL;
  }

  int ret;

  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* file = __get_file(sockfd);
  if (!file) {
    ret = EBADF;
  } else if (!file->socket) {
    ret = ENOTSOCK;
  } else if (file->socket->state != EXE_SOCKET_CONNECTED) {
    ret = ENOTCONN;
  } else {
    // So this is a valid socket
    if ((how == SHUT_RDWR || how == SHUT_RD) && file->socket->readFd != -1) {
      exe_file_t* f = __get_file(file->socket->readFd);
      f->flags &= ~eReadable;

      close(file->socket->readFd);
      file->socket->readFd = -1;
    }

    if ((how == SHUT_RDWR || how == SHUT_WR) && file->socket->writeFd != -1) {
      exe_file_t* f = __get_file(file->socket->writeFd);
      f->flags &= ~eWriteable;

      close(file->socket->writeFd);
      file->socket->writeFd= -1;
    }

    ret = 0;
  }

  if (ret > 0) {
    kpr_handle_fd_changed(file->socket->own_fd);

    if (file->socket->peer) {
      kpr_handle_fd_changed(file->socket->peer->own_fd);
    }
  }

  pthread_mutex_unlock(klee_fs_lock());

  return ret;
}

ssize_t kpr_write_socket(exe_file_t* f, int flags, const void *buf, size_t count) {
  exe_socket_t* s = f->socket;

  if (s->type == SOCK_DGRAM) {
    assert(0 && "unsupported");
  } else if (s->type == SOCK_STREAM) {
    if (f->socket->state != EXE_SOCKET_CONNECTED) {
      errno = ENOTCONN;
      return -1;
    } else if (f->socket->faked_packet) {
      // So we can send as much as we want to this
      // socket

      fprintf(stderr, "KLEE: received [target port=%d, count=%zu]", f->socket->faked_packet->port, count);

      if (write(STDERR_FILENO, buf, count) > 0) {
        char c = '\n';
        write(STDERR_FILENO, &c, 1);
      }

      fflush(stderr);

      return count;
    } 
    
    int write_to = f->socket->peer->writeFd;

    if (write_to == -1) {
      errno = EINVAL;
      return -1;
    } else {
      exe_file_t* pipeFile = __get_file(write_to);
      assert(pipeFile != NULL);

      int origFlags = pipeFile->flags;
      pipeFile->flags = flags;

      ssize_t ret = write(write_to, buf, count);

      pipeFile->flags = origFlags;

      if (ret > 0) {
        kpr_handle_fd_changed(f->socket->peer->own_fd);
      }

      return ret;
    }
  }

  errno = EINVAL;
  return -1;
}

ssize_t kpr_read_socket(exe_file_t* f, int flags, void *buf, size_t count) {
  exe_socket_t* s = f->socket;

  if (s->type == SOCK_DGRAM) {
    assert(0 && "unsupported");
  } else if (s->type == SOCK_STREAM) {
    if (s->state != EXE_SOCKET_CONNECTED) {
      errno = ENOTCONN;
      return -1;
    }
    
    int read_from = s->readFd;

    if (read_from == -1) {
      errno = EINVAL;
      return -1;
    } else {
      exe_file_t* pipeFile = __get_file(read_from);
      assert(pipeFile != NULL);

      int origFlags = pipeFile->flags;
      pipeFile->flags = flags;

      ssize_t ret = read(read_from, buf, count);

      pipeFile->flags = origFlags;

      return ret;
    }
  }

  errno = EINVAL;
  return -1;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
  ssize_t ret;

  if (flags != 0) {
    klee_warning("Ignoring flags for send()");
  }

  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* file = __get_file(sockfd);
  if (!file) {
    ret = -1;
    errno = EBADF;
  } else if (!file->socket) {
    ret = -1;
    errno = ENOTSOCK;
  } else {
    ret = write(sockfd, buf, len);
  }

  pthread_mutex_unlock(klee_fs_lock());

  return ret;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  ssize_t ret;

  if (flags != 0) {
    klee_warning("Ignoring flags for recv()");
  }

  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* file = __get_file(sockfd);
  if (!file) {
    ret = -1;
    errno = EBADF;
  } else if (!file->socket) {
    ret = -1;
    errno = ENOTSOCK;
  } else {
    ret = read(sockfd, buf, len);
  }

  pthread_mutex_unlock(klee_fs_lock());

  return ret;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* f = __get_file(sockfd);
  if (f == NULL) {
    errno = EBADF;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (!f->socket) {
    errno = ENOTSOCK;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (level != SOL_SOCKET || optname != SO_SNDBUF) {
    pthread_mutex_unlock(klee_fs_lock());
    klee_warning("Called getsockopt with unsupported arguments - faked EINTR");
    errno = EINTR;
    return -1;
  }

  if (optval != NULL) {
    int bufSize = PIPE_BUFFER_SIZE;
    if (sizeof(bufSize) != *optlen) {
      pthread_mutex_unlock(klee_fs_lock());
      klee_warning("Called getsockopt with too small optval");
      errno = EINVAL;
      return -1;
    }

    *((int*) optval) = bufSize;
  }

  pthread_mutex_unlock(klee_fs_lock());

  return 0;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* f = __get_file(sockfd);
  if (f == NULL) {
    errno = EBADF;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (!f->socket) {
    errno = ENOTSOCK;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (level == SOL_SOCKET) {
    if (optname == SO_SNDBUF || optname == SO_BROADCAST || optname == SO_KEEPALIVE || optname == SO_REUSEADDR || optname == SO_LINGER) {
      pthread_mutex_unlock(klee_fs_lock());
      klee_warning("Called setsockopt with not yet implemented options - ignoring");
      return 0;
    }
  } else if (level == IPPROTO_TCP) {
    if (optname == TCP_NODELAY) {
      pthread_mutex_unlock(klee_fs_lock());
      klee_warning("Called setsockopt with not yet implemented options - ignoring");
      return 0;
    }
  }

  pthread_mutex_unlock(klee_fs_lock());
  klee_warning("Called setsockopt with unsupported arguments - EINVAL");
  errno = EINVAL;
  return -1;
}

int getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* f = __get_file(sockfd);
  if (f == NULL) {
    errno = EBADF;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (!f->socket) {
    errno = ENOTSOCK;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (!f->socket->saddress) {
    errno = EINVAL; // TODO: find correct one
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  copy_socket_addr_into(f->socket, addr, addrlen);

  pthread_mutex_unlock(klee_fs_lock());

  return 0;
}

int getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  pthread_mutex_lock(klee_fs_lock());

  exe_file_t* f = __get_file(sockfd);
  if (f == NULL) {
    errno = EBADF;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (!f->socket) {
    errno = ENOTSOCK;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (!f->socket->peer) {
    errno = ENOTCONN;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  assert(f->socket->peer != NULL);

  copy_socket_addr_into(f->socket->peer, addr, addrlen);

  pthread_mutex_unlock(klee_fs_lock());
  
  return 0;
}

/* Unsupported operations */

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
  // TODO: implement
  assert(0 && "unsupported");
  return -1;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
  if (flags != 0) {
    errno = EINVAL;
    klee_warning("sendmsg with flags is currently unsupported");
    assert(0 && "Unsupported");
    return -1;
  }

  if (msg->msg_control) {
    errno = EINVAL;
    klee_warning("sendmsg with msg_control is currently unsupported");
    assert(0 && "Unsupported");
    return -1;
  }

  if (msg->msg_name) {
    errno = EINVAL;
    klee_warning("sendmsg with msg_name is currently unsupported");
    assert(0 && "Unsupported");
    return -1;
  }

  pthread_mutex_lock(klee_fs_lock());
  
  exe_file_t* f = __get_file(sockfd);
  if (f == NULL) {
    errno = EBADF;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (!f->socket) {
    errno = ENOTSOCK;
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  ssize_t writtenTotal = 0;

  for (size_t i = 0; i < msg->msg_iovlen; i++) {
    struct iovec* iov = &msg->msg_iov[i];

    ssize_t bytes = write(sockfd, iov->iov_base, iov->iov_len);
    if (bytes < 0) {
      pthread_mutex_unlock(klee_fs_lock());
      return -1;
    }

    writtenTotal += bytes;
  }

  pthread_mutex_unlock(klee_fs_lock());
  return writtenTotal;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
  // TODO: implement
  assert(0 && "unsupported");
  return -1;
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
  // TODO: implement
  assert(0 && "unsupported");
  return -1;
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
  pthread_mutex_lock(klee_fs_lock());

  int fd1 = socket(domain, type, protocol);
  if (fd1 < 0) {
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  int fd2 = socket(domain, type, protocol);
  if (fd2 < 0) {
    close(fd1);
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  exe_socket_t* s1 = __get_file(fd1)->socket;
  exe_socket_t* s2 = __get_file(fd2)->socket;

  establish(s1, s2);

  sv[0] = fd1;
  sv[1] = fd2;

  pthread_mutex_unlock(klee_fs_lock());
  return 0;
}
