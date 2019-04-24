// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --pthread-runtime --exit-on-error --no-schedule-forks --log-por-events %t.bc 2>&1 | FileCheck %s

int main(void) {
  return 0;


  // CHECK: POR event: thread_init with current thread 1 and args: 1
  // CHECK-NEXT: POR event: thread_exit with current thread 1 and args: 1
}
