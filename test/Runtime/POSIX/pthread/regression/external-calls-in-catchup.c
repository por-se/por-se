// RUN: %clang %s -emit-llvm %O0opt -g -c -DTEST_USE_POSIX -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out -posix-runtime -exit-on-error %t.bc

// FIXME: version without TEST_USE_POSIX fails because there are no checks for double-acquires for klee_lock_acquire()

#include "klee/klee.h"

#include <assert.h>
#include <sys/random.h>

#ifdef TEST_USE_POSIX
#include <pthread.h>

pthread_mutex_t locks[] = {
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_MUTEX_INITIALIZER,
};
#else
klee_sync_primitive_t locks[2];
#endif

void locking(void) {
  for (int i = 0; i < 7; i++) {
    unsigned char random_data;
    while (getrandom(&random_data, sizeof(random_data), 0) <= 0);

    int index = random_data % 2;

    assert(index >= 0 && index <= 1);

#ifdef TEST_USE_POSIX
    pthread_mutex_t* lock;
#else
    klee_sync_primitive_t* lock;
#endif

    lock = &locks[index];

#ifdef TEST_USE_POSIX
    pthread_mutex_lock(lock);
    pthread_mutex_unlock(lock);
#else
    klee_lock_acquire(lock);
    klee_lock_release(lock);
#endif
  }
}

#ifdef TEST_USE_POSIX
void*
#else
void
#endif
thread(void* arg) {
  locking();
}

int main(void) {
#ifdef USE_POSIX
  pthread_t th;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&th, &attr, thread, NULL);
#else
  klee_create_thread(thread, NULL);
#endif

  locking();

  return 0;
}
