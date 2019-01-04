; RUN: %opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define void @test() {
entry:
; CHECK-LABEL: entry:
; CHECK-NEXT: br {{.*}} ; live = {}
  br label %reuse

reuse:
; CHECK-LABEL: reuse:
; CHECK-NEXT: %a {{.*}}
; CHECK-NEXT: %x {{.*}} ; live = {%a, %x}
; CHECK-NEXT: %cmp {{.*}} ; live = {%a, %cmp}
; CHECK-NEXT: br {{.*}} ; live = {%a}
  %a = phi i64 [ 0, %entry ], [ 1, %passthrough ]
  %x = phi i64 [ 0, %entry ], [ %a, %passthrough ]
  %cmp = icmp ult i64 %x, 1
  br i1 %cmp, label %passthrough, label %exit

passthrough:
; CHECK-LABEL: passthrough:
; CHECK-NEXT: %dead {{.*}} ; live = {%a}
; CHECK-NEXT: br {{.*}} ; live = {%a}
  %dead = phi i64 [0, %reuse]
  br label %reuse

exit:
; CHECK-LABEL: exit:
; CHECK-NEXT: ret void ; live = {}
  ret void
}
