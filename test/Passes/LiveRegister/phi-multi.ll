; RUN: %opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define void @test() {
entry:
; CHECK-LABEL: entry:
; CHECK-NEXT: br {{.*}} ; live = {}
  br label %a

a:
; CHECK-LABEL: a:
; CHECK-NEXT: %p1 {{.*}}
; CHECK-NEXT: %p2 {{.*}}
; CHECK-NEXT: %p3 {{.*}} ; live = {%p1, %p2, %p3}
; CHECK-NEXT: %y {{.*}} ; live = {%p1, %p2, %p3, %y}
; CHECK-NEXT: %z {{.*}} ; live = {%p1, %p2, %p3, %y, %z}
; CHECK-NEXT: %cmp.a {{.*}} ; live = {%cmp.a, %p2, %p3, %y, %z}
; CHECK-NEXT: br {{.*}} ; live = {%p2, %p3, %y, %z}
  %p1 = phi i64 [ 0, %entry ], [ %z, %a ], [ %x, %b ], [ 3, %c ]
  %p2 = phi i64 [ 0, %entry ], [ %y, %a ], [ 2, %b ], [ %y, %c ]
  %p3 = phi i64 [ 0, %entry ], [ 1, %a ], [ %x, %b ], [ %z, %c ]
  %y = add i64 %p3, 5
  %z = add i64 %p2, 1
  %cmp.a = icmp ult i64 %p1, 1
  br i1 %cmp.a, label %a, label %b

b:
; CHECK-LABEL: b:
; CHECK-NEXT: %x {{.*}} ; live = {%p2, %p3, %x, %y, %z}
; CHECK-NEXT: %cmp.b {{.*}} ; live = {%cmp.b, %p3, %x, %y, %z}
; CHECK-NEXT: br {{.*}} ; live = {%p3, %x, %y, %z}
  %x = phi i64 [ 0, %a ]
  %cmp.b = icmp ult i64 %p2, 2
  br i1 %cmp.b, label %a, label %c

c:
; CHECK-LABEL: c:
; CHECK-NEXT: %cmp.c {{.*}} ; live = {%cmp.c, %y, %z}
; CHECK-NEXT: br {{.*}} ; live = {%y, %z}
  %cmp.c = icmp ult i64 %p3, 3
  br i1 %cmp.c, label %a, label %exit

exit:
; CHECK-LABEL: exit:
; CHECK-NEXT: ret void ; live = {}
  ret void
}
