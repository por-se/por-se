; RUN: opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define void @test() {
entry:
; CHECK-LABEL: entry:
; CHECK-NEXT: br {{.*}} ; live = {}
  br label %a

a:
; CHECK-LABEL: a:
; CHECK-NEXT: %z {{.*}} ; live = {%z}
; CHECK-NEXT: %x {{.*}} ; live = {%x, %z}
; CHECK-NEXT: %cmp.a {{.*}} ; live = {%cmp.a, %z}
; CHECK-NEXT: br {{.*}} ; live = {%z}
  %z = phi i64 [ 0, %entry ], [ 4, %a ]
  %x = add i64 %z, 1
  %cmp.a = icmp ult i64 %x, 1
  br i1 %cmp.a, label %a, label %b

b:
; CHECK-LABEL: b:
; CHECK-NEXT: %y {{.*}} ; live = {%y, %z}
; CHECK-NEXT: %cmp.b {{.*}} ; live = {%cmp.b, %z}
; CHECK-NEXT: br {{.*}} ; live = {%z}
  %y = phi i64 [ 0, %c ], [ %z, %a ]
  %cmp.b = icmp ult i64 %y, 2
  br i1 %cmp.b, label %exit, label %c

c:
; CHECK-LABEL: c:
; CHECK-NEXT: %cmp {{.*}} ; live = {%z}
; CHECK-NEXT: br {{.*}} ; live = {%z}
  %cmp = icmp ult i64 %z, 3
  br label %b

exit:
; CHECK-LABEL: exit:
; CHECK-NEXT: ret void ; live = {}
  ret void
}
