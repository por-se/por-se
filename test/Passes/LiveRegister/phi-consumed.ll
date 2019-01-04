; RUN: %opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define void @test() {
entry:
  br label %a

a:
; CHECK-LABEL: a:
; CHECK-NEXT: %x = phi {{.*}} ; live = {%x}
; CHECK-NEXT: %cmp = {{.*}} ; live = {%cmp}
; CHECK-NEXT: br {{.*}} ; live = {}
  %x = phi i64 [ 0, %entry ], [ %y, %b ]
  %cmp = icmp ult i64 %x, 1
  br i1 %cmp, label %b, label %c

b:
; CHECK-LABEL: b:
; CHECK-NEXT: %y = phi {{.*}} ; live = {%y}
; CHECK-NEXT: br {{.*}} ; live = {%y}
  %y = phi i64 [0, %a]
  br label %a

c:
  ret void
}
