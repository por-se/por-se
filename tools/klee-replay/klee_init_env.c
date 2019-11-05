#define KLEE_REPLAY_INIT_ENV
#include "../../runtime/POSIX/klee_init_env.c"

int __klee_posix_wrapped_main(int argc, char **argv) { return 0; }

static pthread_mutex_t fs_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

pthread_mutex_t* klee_fs_lock(void) {
  return &fs_lock;
}
