#ifndef KPR_NET_SOCKET_INTERNAL_H
#define KPR_NET_SOCKET_INTERNAL_H

#include "klee/runtime/kpr/ringbuffer.h"

#include <arpa/inet.h>

typedef struct {
  struct sockaddr* addr;
  socklen_t addr_len;

  kpr_ringbuffer data;
} kpr_udp_data;

#endif // KPR_NET_SOCKET_INTERNAL_H
