#include "klee/klee.h"
#include "klee/runtime/kpr/list.h"

#include "../fd.h"
#include "socket.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <assert.h>

static kpr_list open_sockets = KPR_LIST_INITIALIZER;
static kpr_list waiting_sockets = KPR_LIST_INITIALIZER;

// Just assume that we do not have any conflict
static int port_counter = 49152;

static exe_socket_t* get_socket_by_port(int port) {
  kpr_list_iterator it = kpr_list_iterate(&open_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket->opened_port == port) {
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
  pthread_cond_init(&socket->cond, NULL);

  f->socket = socket;
  socket->own_fd = fd;

  if (target) {
    *target = socket;
  }

  return fd;
}

static void remove_from_waiting(exe_socket_t* s) {
  kpr_list_iterator it = kpr_list_iterate(&waiting_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket == s) {
      kpr_list_erase(&waiting_sockets, &it);
      return;
    }

    kpr_list_iterator_next(&it);
  }
}

static exe_socket_t* find_waiting_by_req_port(int port) {
  kpr_list_iterator it = kpr_list_iterate(&waiting_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket->requested_port == port) {
      kpr_list_erase(&waiting_sockets, &it);
      return socket;
    }

    kpr_list_iterator_next(&it);
  }

  return NULL;
}

static void check_for_sym_ports(exe_socket_t* socket) {
  assert(socket->state == EXE_SOCKET_PASSIVE);

  kpr_list_iterator it = kpr_list_iterate(&__exe_env.sym_port);
  while(kpr_list_iterator_valid(it)) {
    exe_sym_port_t* sym_port = kpr_list_iterator_value(it);

    if (socket->opened_port == sym_port->port) {
      exe_socket_t* symSocket;
      int fd = create_socket(&symSocket);

      if (fd < 0) {
        klee_warning("could not create socket for sym port - aborting");
        return;
      }

      symSocket->state = EXE_SOCKET_CONNECTING;
      symSocket->requested_port = sym_port->port;
      symSocket->readFd = 0;
      symSocket->writeFd = 0;
      symSocket->sym_port = sym_port;

      kpr_list_push(&socket->queued_peers, symSocket);

      // Only remove once we set up the requesting peer
      kpr_list_erase(&__exe_env.sym_port, &it);
    }

    kpr_list_iterator_next(&it);
  }
}

int socket(int domain, int typeAndFlags, int protocol) {
  // TODO: handle protocol a bit better

  if (domain != AF_INET) {
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

  int fd = create_socket(NULL);
  exe_file_t* f = __get_file(fd);
  f->flags |= flags;

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

  if (f->socket->state != EXE_SOCKET_INIT || addrlen < sizeof(struct sockaddr_in)) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EINVAL;
    return -1;
  }

  f->socket->opened_port = ((struct sockaddr_in*) addr)->sin_port;
  f->socket->state = EXE_SOCKET_BOUND;

  pthread_mutex_unlock(klee_fs_lock());
  return 0;
}

int listen(int sockfd, int backlog) {
  pthread_mutex_lock(klee_fs_lock());
  exe_file_t* f = __get_file(sockfd);
  if (!f->socket) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = ENOTSOCK;
    return -1;
  }

  if (f->socket->state != EXE_SOCKET_BOUND) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EOPNOTSUPP;
    return -1;
  }

  if (get_socket_by_port(f->socket->opened_port) != NULL) {
    pthread_mutex_unlock(klee_fs_lock());
    errno = EADDRINUSE;
    return -1;
  }

  f->socket->state = EXE_SOCKET_PASSIVE;

  // Adding it to the list of known sockets
  kpr_list_push(&open_sockets, f->socket);

  check_for_sym_ports(f->socket);

  pthread_mutex_unlock(klee_fs_lock());
  return 0;
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

  passive->readFd = pipeOne[0];
  passive->writeFd = pipeTwo[1];

  connecting->readFd = pipeTwo[0];
  connecting->writeFd = pipeOne[1];

  // Now mark them also as connected
  connecting->state = EXE_SOCKET_CONNECTED;
  passive->state = EXE_SOCKET_CONNECTED;

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

    peer = find_waiting_by_req_port(f->socket->opened_port);
    if (peer != NULL) {
      assert(peer->state == EXE_SOCKET_CONNECTING);
      break;
    }

    pthread_cond_wait(&f->socket->cond, klee_fs_lock());
  }

  // Now we have to create yet another socket that is used for the actual communication
  exe_socket_t* newSocket;
  int newSocketFd = create_socket(&newSocket);
  if (newSocketFd < 0) {
    kpr_list_push(&f->socket->queued_peers, peer);
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (establish(newSocket, peer) < 0) {
    kpr_list_push(&f->socket->queued_peers, peer);
    pthread_mutex_unlock(klee_fs_lock());
    return -1;
  }

  if (addr != NULL) {
    if (*addrlen < sizeof(struct sockaddr_in)) {
      *addrlen = 0;
    } else {
      struct sockaddr_in *faked_peer = (struct sockaddr_in *) addr;

      // Fake that we connected from localhost
      faked_peer->sin_addr.s_addr = (127 << 24) | (0 << 16) | (0 << 8) | 1;
      faked_peer->sin_port = peer->opened_port;
    }
  }

  // Just hope that we do not have any conflict ...
  newSocket->opened_port = port_counter++;
  assert(port_counter <= 65535);

  // And wake up the peer
  if (peer->sym_port == NULL) {
    pthread_cond_signal(&peer->cond);
  } else {
    // If we write too many bytes, then we risk blocking this
    assert(peer->sym_port->packet_length <= PIPE_BUFFER_SIZE);
    ssize_t ctn = write(peer->writeFd, peer->sym_port->data, peer->sym_port->packet_length);
    if (ctn < 0) {
      klee_warning("Failed to write the symbolic data");
    } else if (ctn != peer->sym_port->packet_length) {
      klee_warning("Failed to write all symbolic data - only parts");
    }

    close(peer->own_fd);
  }

  pthread_mutex_unlock(klee_fs_lock());
  return newSocketFd;
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

  int requestedPort = ((struct sockaddr_in*) addr)->sin_port;

  f->socket->state = EXE_SOCKET_CONNECTING;
  f->socket->requested_port = requestedPort;
  f->socket->readFd = 0;
  f->socket->writeFd = 0;

  bool inList = false;
  exe_socket_t* socket = NULL;

  while (1) {
    socket = get_socket_by_port(requestedPort);

    if (socket) {
      if (socket->state != EXE_SOCKET_PASSIVE) {
        if (inList) {
          remove_from_waiting(f->socket);
        }

        pthread_mutex_unlock(klee_fs_lock());
        errno = ECONNREFUSED;
        return -1;
      } else {
        if (inList) {
          remove_from_waiting(f->socket);
        }

        break;
      }
    }

    if (!inList) {
      kpr_list_push(&waiting_sockets, f->socket);
      inList = true;
    }

    pthread_cond_wait(&f->socket->cond, klee_fs_lock());
  }

  assert(socket != NULL);
  if (inList) {
    remove_from_waiting(f->socket);
  }

  inList = false;
  while (f->socket->readFd == 0 && f->socket->writeFd == 0) {
    // Connection was not established, but we now know that there
    // is a socket waiting
    if (!inList) {
      kpr_list_push(&socket->queued_peers, f->socket);
      inList = true;
    }

    pthread_cond_signal(&socket->cond);
    pthread_cond_wait(&f->socket->cond, klee_fs_lock());
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

  kpr_list_iterator it = kpr_list_iterate(&open_sockets);
  while(kpr_list_iterator_valid(it)) {
    exe_socket_t* socket = kpr_list_iterator_value(it);

    if (socket == file->socket) {
      kpr_list_erase(&open_sockets, &it);
    }

    kpr_list_iterator_next(&it);
  }

  pthread_cond_destroy(&file->socket->cond);

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
    if ((how == SHUT_RDWR || how == SHUT_RD) && file->socket->readFd != 0) {
      exe_file_t* f = __get_file(file->socket->readFd);
      f->flags &= ~eReadable;

      close(file->socket->readFd);
      file->socket->readFd = 0;
    }

    if ((how == SHUT_RDWR || how == SHUT_WR) && file->socket->writeFd != 0) {
      exe_file_t* f = __get_file(file->socket->writeFd);
      f->flags &= ~eWriteable;

      close(file->socket->writeFd);
      file->socket->writeFd= 0;
    }

    ret = 0;
  }

  pthread_mutex_unlock(klee_fs_lock());

  return ret;
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

/* Unsupported operations */

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
  // TODO: implement
  assert(0 && "unsupported");
  return -1;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
  // TODO: implement
  assert(0 && "unsupported");
  return -1;
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