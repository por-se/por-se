; RUN: %opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define void @test() {
entry:
; CHECK-LABEL: entry:
; CHECK-NEXT: %z = {{.*}} ; live = {%z}
; CHECK-NEXT: %cmp = {{.*}} ; live = {%cmp, %z}
; CHECK-NEXT: br {{.*}} ; live = {%z}
  %z = add i64 0, 1
  %cmp = icmp ult i64 %z, 0
  br i1 %cmp, label %noconsume, label %phiconsume

noconsume:
; CHECK-LABEL: noconsume:
; CHECK-NEXT: %x = phi {{.*}} ; live = {%x}
; CHECK-NEXT: %y = {{.*}} ; live = {%x}
  %x = phi i64 [ %z, %entry ], [ %x, %noconsume ]
  %y = add i64 0, 1
  br label %noconsume

phiconsume:
; CHECK-LABEL: phiconsume:
; CHECK-NEXT: %i = phi {{.*}} ; live = {%i}
; CHECK-NEXT: %i.i = {{.*}} ; live = {%i.i}
; CHECK-NEXT: br {{.*}} ; live = {%i.i}
  %i = phi i64 [ %z, %entry ], [ %i.i, %phiconsume ]
  %i.i = add i64 %i, 1
  br label %phiconsume
}
