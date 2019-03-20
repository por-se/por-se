#ifndef KLEE_POR_EVENTS_H
#define KLEE_POR_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  por_empty = 0,

  por_local = 1,
  por_program_init = 2,
  por_thread_create = 3,
  por_thread_join = 4,
  por_thread_init = 5,
  por_thread_exit = 6,
  por_lock_create = 7,
  por_lock_destroy = 8,
  por_lock_acquire = 9,
  por_lock_release = 10,
  por_condition_variable_create = 11,
  por_condition_variable_destroy = 12,
  por_wait1 = 13,
  por_wait2 = 14,
  por_signal = 15,
  por_broadcast = 16
} por_event_t;

#ifdef __cplusplus
}
#endif

#endif //KLEE_POR_EVENTS_H
