//===-- fd_64.c -----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if __GNUC__
#if __x86_64__ || __ppc64__
#define ENV64
#else
#define ENV32
#endif
#endif

#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include "fd.h"

#include "klee/Config/Version.h"
#include "klee/klee.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#ifndef __FreeBSD__
#include <sys/vfs.h>
#endif
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/*** Forward to actual implementations ***/

int open(const char *pathname, int flags, ...) {
  pthread_mutex_lock(klee_fs_lock());
  mode_t mode = 0;

  if (flags & O_CREAT) {
    /* get mode */
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }

  int ret = __fd_open(pathname, flags, mode);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int openat(int fd, const char *pathname, int flags, ...) {
  pthread_mutex_lock(klee_fs_lock());
  mode_t mode = 0;

  if (flags & O_CREAT) {
    /* get mode */
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }

  int ret = __fd_openat(fd, pathname, flags, mode);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

off64_t lseek(int fd, off64_t offset, int whence) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_lseek(fd, offset, whence);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int __xstat(int vers, const char *path, struct stat *buf) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_stat(path, (struct stat64*) buf);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int stat(const char *path, struct stat *buf) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_stat(path, (struct stat64*) buf);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int __lxstat(int vers, const char *path, struct stat *buf) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_lstat(path, (struct stat64*) buf);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int lstat(const char *path, struct stat *buf) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_lstat(path, (struct stat64*) buf);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int __fxstat(int vers, int fd, struct stat *buf) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_fstat(fd, (struct stat64*) buf);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int fstat(int fd, struct stat *buf) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_fstat(fd, (struct stat64*) buf);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int ftruncate64(int fd, off64_t length) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_ftruncate(fd, length);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

int statfs(const char *path, struct statfs *buf) __attribute__((weak));
int statfs(const char *path, struct statfs *buf) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_statfs(path, buf);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}

ssize_t getdents64(int fd, void *dirp, size_t count) {
  pthread_mutex_lock(klee_fs_lock());
  int ret = __fd_getdents(fd, (struct dirent64*) dirp, count);
  pthread_mutex_unlock(klee_fs_lock());
  return ret;
}
ssize_t __getdents64(int fd, void *dirp, size_t count)
     __attribute__((alias("getdents64")));
