#ifndef KPR_NET_SOCKET_H
#define KPR_NET_SOCKET_H

#include "../fd.h"

int kpr_close_socket(exe_file_t* f);

ssize_t kpr_write_socket(exe_file_t* f, int flags, const void *buf, size_t count);
ssize_t kpr_read_socket(exe_file_t* f, int flags, void *buf, size_t count);

#endif // KPR_NET_SOCKET_H
