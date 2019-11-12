// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error %t.bc --sym-packet 80 1  2>&1 | FileCheck %s

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netinet/ip.h>
#include <assert.h>

int main() {
  int sfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(sfd > 0);

  struct sockaddr_in isa;
  memset(&isa, 0, sizeof(isa));
  isa.sin_port = 80;

  assert(bind(sfd, (struct sockaddr*)&isa, sizeof isa) == 0);
  assert(listen(sfd, 1) == 0);

  int cfd = accept(sfd, NULL, NULL);
  assert(cfd > 0);

  char data[1];
  assert(recv(cfd, data, 1, 0) == 1);

  if (data[0] == 'A') {
    puts("Was A");
  } else {
    puts("Was Not A");
  }

  // CHECK: KLEE: done: completed paths = 2

  assert(shutdown(cfd, SHUT_RDWR) == 0);
  return 0;
}