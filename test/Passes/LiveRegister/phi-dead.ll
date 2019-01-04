; RUN: %opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define void @test() {
entry:
; CHECK-LABEL: entry:
; CHECK-NEXT: br {{.*}} ; live = {}
  br label %neverlive

neverlive:
; CHECK-LABEL: neverlive:
; CHECK-NEXT: %x {{.*}}
; CHECK-NEXT: %a {{.*}} ; live = {%x}
; CHECK-NEXT: %cmp {{.*}} ; live = {%cmp}
; CHECK-NEXT: br {{.*}} ; live = {}
  %x = phi i64 [ 0, %entry ], [ 1, %neverlive ]
  %a = phi i64 [ 0, %entry ], [ 0, %neverlive ]
  %cmp = icmp ult i64 %x, 1
  br i1 %cmp, label %neverlive, label %reallydead

reallydead:
; CHECK-LABEL: reallydead:
; CHECK-NEXT: %dead {{.*}} ; live = {}
; CHECK-NEXT: br {{.*}} ; live = {}
  %dead = phi i64 [0, %neverlive]
  br label %exit

exit:
; CHECK-LABEL: exit:
; CHECK-NEXT: ret void ; live = {}
  ret void
}
