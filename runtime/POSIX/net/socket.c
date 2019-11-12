#include "klee/klee.h"
#include "klee/runtime/kpr/list.h"

#include "../fd.h"

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
static int port_counter = 12042;

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
  f->flags = eOpen;
  kpr_list_create(&f->notification_list);

  exe_socket_t* socket = calloc(sizeof(exe_socket_t), 1);
  f->socket = socket;

  socket->state = EXE_SOCKET_INIT;
  pthread_cond_init(&socket->cond, NULL);

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

int socket(int domain, int typeAndFlags, int protocol) {
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

  // TODO: check whether we want to send symbolic data on the port

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

  // And wake up the peer
  pthread_cond_signal(&peer->cond);

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
