#define KLEE_REPLAY_INIT_ENV
#include "../../runtime/POSIX/klee_init_env.c"

int __klee_posix_wrapped_main(int argc, char **argv, char **envp) { return 0; }
