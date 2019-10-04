// XFAIL: asan
// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t-missing.klee-out
// RUN: rm -rf %t-address.klee-out
// RUN: rm -rf %t-overlapping.klee-out
// RUN: rm -rf %t-syntax.klee-out
// RUN: rm -rf %t-tid.klee-out
// RUN: not %klee --output-dir=%t-missing.klee-out --allocate-thread-heap-size=50 --allocate-thread-segments-file=%p/thread-mappings-404.conf %t.bc 2>&1 | FileCheck --check-prefix "CHECK-MISSING" %s
// RUN: not %klee --output-dir=%t-address.klee-out --allocate-thread-heap-size=50 --allocate-thread-segments-file=%p/thread-mappings-invalid-address.conf %t.bc 2>&1 | FileCheck --check-prefix "CHECK-ADDRESS" %s
// RUN: not %klee --output-dir=%t-overlapping.klee-out --allocate-thread-heap-size=50 --allocate-thread-segments-file=%p/thread-mappings-invalid-overlapping.conf %t.bc 2>&1 | FileCheck --check-prefix "CHECK-OVERLAPPING" %s
// RUN: not %klee --output-dir=%t-syntax.klee-out --allocate-thread-heap-size=50 --allocate-thread-segments-file=%p/thread-mappings-invalid-syntax.conf %t.bc 2>&1 | FileCheck --check-prefix "CHECK-SYNTAX" %s
// RUN: not %klee --output-dir=%t-tid.klee-out --allocate-thread-heap-size=50 --allocate-thread-segments-file=%p/thread-mappings-invalid-tid.conf %t.bc 2>&1 | FileCheck --check-prefix "CHECK-TID" %s


#include <stdlib.h>
#include <assert.h>

int main(int argc, char **argv) {
  // CHECK-MISSING: KLEE: ERROR: Could not open the segments file specified by -allocate-thread-segments-file
  // CHECK-TID: KLEE: ERROR: ThreadId in -allocate-thread-segments-file in line 3 malformed. Exiting.
  // CHECK-ADDRESS: KLEE: ERROR: Address specified in -allocate-thread-segments-file in line 3 may not be zero. Exiting.
  // CHECK-OVERLAPPING: KLEE: ERROR: Overlapping mapping requested
  // CHECK-SYNTAX: KLEE: ERROR: Line 3 in -allocate-thread-segments-file malformed. Expected '='. Exiting.

  assert(0);

  return 0;
}
