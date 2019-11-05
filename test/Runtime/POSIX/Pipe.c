// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t-first.klee-out
// RUN: rm -rf %t-last.klee-out
// RUN: rm -rf %t-random.klee-out
// RUN: rm -rf %t-round-robin.klee-out
// RUN: %klee --posix-runtime --output-dir=%t-first.klee-out --thread-scheduling=first %t.bc
// RUN: %klee --posix-runtime --output-dir=%t-last.klee-out --thread-scheduling=last %t.bc
// RUN: %klee --posix-runtime --output-dir=%t-random.klee-out --thread-scheduling=random %t.bc
// RUN: %klee --posix-runtime --output-dir=%t-round-robin.klee-out --thread-scheduling=round-robin %t.bc

#include <unistd.h>
#include <sys/poll.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>

void* thread(void* arg) {
  int fd = *((int*) arg);
  char* buf = "Hello World!";
  
  for (int i = 0; i < 5; i++) {
    ssize_t count = write(fd, buf, 13);
    assert(count == 13);
  }

  return NULL;
}

int main (void) {
  int pipeFd[2];

  if (pipe(pipeFd) != 0) {
    return 1;
  }

  pthread_t th;
  pthread_create(&th, NULL, thread, &pipeFd[1]);

  for (int i = 0; i < 5; i++) {
    char buffer[14];
    ssize_t count = read(pipeFd[0], buffer, 13);

    buffer[13] = '\0';
    assert(strcmp(buffer, "Hello World!") == 0);
  }

  pthread_join(th, NULL);

  return 0;
}