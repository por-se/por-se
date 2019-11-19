//===-- fd.h ---------------------------------------------------*- C++ -*--===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_FD_H
#define KLEE_FD_H

#include "klee/Config/config.h"
#include "klee/runtime/kpr/list-types.h"
#include "klee/runtime/kpr/ringbuffer.h"

#include <pthread.h>
#include <stdbool.h>

#ifndef _LARGEFILE64_SOURCE
#error "_LARGEFILE64_SOURCE should be defined"
#endif

#include <dirent.h>
#include <sys/types.h>

#ifdef HAVE_SYSSTATFS_H
#include <sys/statfs.h>
#endif

#ifdef __APPLE__
#include <sys/dtrace.h>
#endif
#ifdef __FreeBSD__
#include "FreeBSD.h"
#endif
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/mount.h>
#include <sys/param.h>
#if !defined(dirent64)
#define dirent64 dirent
#endif
#endif

typedef struct {
  int port;
  int packet_length;

  char* data;
} exe_fake_packet_t;

typedef struct {
  unsigned size;  /* in bytes */
  char* contents;
  struct stat64* stat;
} exe_disk_file_t;

typedef enum {
  eOpen         = (1 << 0),
  eCloseOnExec  = (1 << 1),
  eReadable     = (1 << 2),
  eWriteable    = (1 << 3),
  eNonBlock     = (1 << 4)
} exe_file_flag_t;

#define EXE_SOCKET_INIT (1)
#define EXE_SOCKET_BOUND (2)
#define EXE_SOCKET_CONNECTING (3)
#define EXE_SOCKET_PASSIVE (4)
#define EXE_SOCKET_CONNECTED (5)

typedef struct exe_socket {
  int state;
  int own_fd;

  // General options
  int domain;
  int type;

  struct {
    bool reuseAddress;
    bool keepAlive;
    bool tcpNoDelay;
    bool broadcast;
  } options;

  pthread_t blocked_thread;

  struct sockaddr *saddress;
  size_t saddress_len;

  // Needed for passive sockets
  kpr_list queued_peers;
  kpr_list blocked_threads;

  union {
    int port;
    char* path;
  } requested;

  union {
    int port;
    char* path;
  } opened;

  union {
    struct kpr_tcp {
      struct exe_socket* peer;
      kpr_ringbuffer buffer;
    } tcp;

    struct kpr_udp {
      kpr_list data;
    } udp;
  } proto;

  // If this is a sym socket port
  exe_fake_packet_t* faked_packet;
} exe_socket_t;

#define PIPE_BUFFER_SIZE 2048

typedef struct {
  kpr_ringbuffer buffer;

  int readFd;
  int writeFd;

  kpr_list blocked_threads;
} exe_pipe_t;

typedef struct {
  int fd;                   /* actual fd if not symbolic */
  unsigned flags;           /* set of exe_file_flag_t values. fields
                               are only defined when flags at least
                               has eOpen. */
  off64_t off;              /* offset */
  exe_disk_file_t* dfile;   /* ptr to file on disk, if symbolic */
  exe_pipe_t* pipe;         /* ptr to the pipe, if own pipe */
  exe_socket_t* socket;

  kpr_list notification_list;  /* should be notified about possible changes to the file */
} exe_file_t;

typedef struct {
  unsigned n_sym_files; /* number of symbolic input files, excluding stdin */
  exe_disk_file_t *sym_stdin, *sym_stdout;
  unsigned stdout_writes; /* how many chars were written to stdout */
  exe_disk_file_t *sym_files;
  /* --- */
  /* the maximum number of failures on one path; gets decremented after each failure */
  unsigned max_failures; 

  /* Which read, write etc. call should fail */
  int *read_fail, *write_fail, *close_fail, *ftruncate_fail, *getcwd_fail;
  int *chmod_fail, *fchmod_fail;
} exe_file_system_t;

#define MAX_FDS 128

/* Note, if you change this structure be sure to update the
   initialization code if necessary. New fields should almost
   certainly be at the end. */
typedef struct {
  exe_file_t fds[MAX_FDS];
  mode_t umask; /* process umask */
  unsigned version;
  /* If set, writes execute as expected.  Otherwise, writes extending
     the file size only change the contents up to the initial
     size. The file offset is always incremented correctly. */
  int save_all_writes;

  kpr_list fake_packets;
} exe_sym_env_t;

extern exe_file_system_t __exe_fs;
extern exe_sym_env_t __exe_env;

void klee_init_sym_port(int port, int len);
void klee_init_fake_packet(int port, const char* data, int len);

void klee_init_fds(unsigned n_files, unsigned file_length,
                   unsigned stdin_length, int sym_stdout_flag,
                   int do_all_writes_flag, unsigned max_failures);
void klee_init_env(int *argcPtr, char ***argvPtr);

/* *** */

pthread_mutex_t* klee_fs_lock(void);
exe_file_t *__get_file(int fd);
exe_file_t *__get_file_ignore_flags(int fd);
int __get_unused_fd(void);

void notify_thread_list(kpr_list* blocked_threads);

/* *** */

int __fd_open(const char *pathname, int flags, mode_t mode);
int __fd_openat(int basefd, const char *pathname, int flags, mode_t mode);
off64_t __fd_lseek(int fd, off64_t offset, int whence);
int __fd_stat(const char *path, struct stat64 *buf);
int __fd_lstat(const char *path, struct stat64 *buf);
int __fd_fstat(int fd, struct stat64 *buf);
int __fd_ftruncate(int fd, off64_t length);
int __fd_statfs(const char *path, struct statfs *buf);
int __fd_getdents(unsigned int fd, struct dirent64 *dirp, unsigned int count);

#endif /* KLEE_FD_H */
