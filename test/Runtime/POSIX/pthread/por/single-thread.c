// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --posix-runtime --exit-on-error --debug-event-registration %t.bc 2>&1 | FileCheck %s

int main(void) {
  return 0;

  // CHECK: [state id: 0] registering thread_init with current thread [[M_TID:[0-9,]+]] and initialized thread [[M_TID]]
  // CHECK-DAG: [state id: 0] registering thread_exit with current thread [[M_TID]] and exited thread [[M_TID]]
}
