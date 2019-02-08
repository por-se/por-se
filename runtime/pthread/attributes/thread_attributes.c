#include "klee/klee.h"
#include "klee/runtime/pthread.h"

/*
 * Thread attributes
 */

int pthread_attr_init(pthread_attr_t *attr) {
  attr->detachstate = PTHREAD_CREATE_JOINABLE;
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
  return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t * attr, int * detachstate) {
  *detachstate = attr->detachstate;
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
  if (detachstate != PTHREAD_CREATE_JOINABLE && detachstate != PTHREAD_CREATE_DETACHED) {
    klee_report_error(__FILE__, __LINE__, "trying to set a thread detachstate that is unknown", "user");
  }

  attr->detachstate = detachstate;
  return 0;
}