; RUN: rm -rf %t.klee-out
; RUN: %klee -output-dir=%t.klee-out -debug-print-instructions=all:stderr -debug-live-set -debug-cutoff-events %s 2>&1 | FileCheck %s

define i32 @main() {
entry:
  %atomic = alloca i8, align 8
; CHECK: load atomic
; CHECK-NEXT: liveSet: {}
  load atomic i8, i8* %atomic acquire, align 1

; CHECK-NOT: cutoff

; CHECK: KLEE: done: total instructions = 3
  ret i32 0
}
