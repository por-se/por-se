; RUN: %opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define void @test() {
entry:
; CHECK-LABEL: entry:
; CHECK-NEXT: %ptr = {{.*}} ; live = {%ptr}
; CHECK-NEXT: store {{.*}} ; live = {%ptr}
; CHECK-NEXT: %p = {{.*}} ; live = {%p}
; CHECK-NEXT: %cmp1 = {{.*}} ; live = {%cmp1, %p}
; CHECK-NEXT: br {{.*}} ; live = {%p}
  %ptr = alloca i64
  store i64 3, i64* %ptr
  %p = load i64, i64* %ptr
  %cmp1 = icmp ult i64 %p, 5
  br i1 %cmp1, label %twophi, label %onephi

twophi:
; CHECK-LABEL: twophi:
; CHECK-NEXT: %x = {{.*}}
; CHECK-NEXT: %y = {{.*}} ; live = {%x, %y}
; CHECK-NEXT: %cmp2 = {{.*}} ; live = {%cmp2, %x}
; CHECK-NEXT: br {{.*}} ; live = {%x}
  %x = phi i64 [ %p, %entry ], [ 0, %onephi ]
  %y = phi i64 [ 0, %entry ], [ %z, %onephi ]
  %cmp2 = icmp ult i64 %x, %y
  br i1 %cmp2, label %onephi, label %oneinst

onephi:
; CHECK-LABEL: onephi:
; CHECK-NEXT: %c = {{.*}} ; live = {%c}
; CHECK-NEXT: %z = {{.*}} ; live = {%c, %z}
; CHECK-NEXT: %cmp3 = {{.*}} ; live = {%cmp3, %z}
; CHECK-NEXT: br {{.*}} ; live = {%z}
  %c = phi i64 [ %x, %twophi ], [ 0, %entry]
  %z = add i64 %c, 1
  %cmp3 = icmp ult i64 %z, %c
  br i1 %cmp3, label %twophi, label %twoinst

twoinst:
; CHECK-LABEL: twoinst:
; CHECK-NEXT: %h = {{.*}} ; live = {}
; CHECK-NEXT: ret void ; live = {}
  %h = xor i64 0, 0
  ret void

oneinst:
; CHECK-LABEL: oneinst:
; CHECK-NEXT: ret void ; live = {}
  ret void
}
