; RUN: %klee -debug-print-instructions=all:stderr -debug-live-set %s 2>&1 | FileCheck %s
; test program originally copied from test/Passes/LiveRegister/phi-multi.ll
; but extended to test live sets with function call and return

define i64 @add(i64 %d, i64 %e) {
entry:
  %g = add i64 %d, %e
  %h = add i64 %e, 6
  %i = sub i64 %h, %d
  ret i64 %g
}

define void @main() {
entry:
; CHECK: br label %a
; CHECK-NEXT: before {{.*}} liveSet: {}
; CHECK-NEXT:  after {{.*}} liveSet: {}
  br label %a

a:
; CHECK: %p1 = phi
; CHECK-NEXT: before {{.*}} liveSet: {}
; CHECK-NEXT:  after {{.*}} liveSet: {}
  %p1 = phi i64 [ 0, %entry ], [ %z, %a ], [ %x, %b ], [ 3, %c ]
; CHECK: %p2 = phi
; CHECK-NEXT: before {{.*}} liveSet: {}
; CHECK-NEXT:  after {{.*}} liveSet: {}
  %p2 = phi i64 [ 0, %entry ], [ %y, %a ], [ 2, %b ], [ %y, %c ]
; CHECK: %p3 = phi
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3}
  %p3 = phi i64 [ 0, %entry ], [ 1, %a ], [ %x, %b ], [ %z, %c ]
; CHECK: %y = call
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3 %y}
; CHECK-NEXT:  after {{.*}} liveSet: {}
  %y = call i64 @add(i64 %p3, i64 5)
; going into @add()

; CHECK: %g = add
; CHECK-NEXT: before {{.*}} liveSet: {%g}
; CHECK-NEXT:  after {{.*}} liveSet: {%g}
; CHECK: %h = add
; CHECK-NEXT: before {{.*}} liveSet: {%g %h}
; CHECK-NEXT:  after {{.*}} liveSet: {%g %h}
; CHECK: %i = sub
; CHECK-NEXT: before {{.*}} liveSet: {%g}
; CHECK-NEXT:  after {{.*}} liveSet: {%g}
; CHECK: ret i64
; CHECK-NEXT: before {{.*}} liveSet: {}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3 %y}

; CHECK: %z = add
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3 %y %z}
  %z = add i64 %p2, 1
; CHECK: %cmp.a = icmp
; CHECK-NEXT: before {{.*}} liveSet: {%cmp.a %p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%cmp.a %p2 %p3 %y %z}
  %cmp.a = icmp ult i64 %p1, 1
; CHECK: br i1 %cmp.a
; CHECK-NEXT: before {{.*}} liveSet: {%p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p2 %p3 %y %z}
  br i1 %cmp.a, label %a, label %b

; going to label %a again
; CHECK: %p1 = phi
; CHECK-NEXT: before {{.*}} liveSet: {%p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p2 %p3 %y %z}
; CHECK: %p2 = phi
; CHECK-NEXT: before {{.*}} liveSet: {%p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p2 %p3 %y %z}
; CHECK: %p3 = phi
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3}
; CHECK: %y = call
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3 %y}
; CHECK-NEXT:  after {{.*}} liveSet: {}
; CHECK: %g = add
; CHECK-NEXT: before {{.*}} liveSet: {%g}
; CHECK-NEXT:  after {{.*}} liveSet: {%g}
; CHECK: %h = add
; CHECK-NEXT: before {{.*}} liveSet: {%g %h}
; CHECK-NEXT:  after {{.*}} liveSet: {%g %h}
; CHECK: %i = sub
; CHECK-NEXT: before {{.*}} liveSet: {%g}
; CHECK-NEXT:  after {{.*}} liveSet: {%g}
; CHECK: ret i64
; CHECK-NEXT: before {{.*}} liveSet: {}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3 %y}
; CHECK: %z = add
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3 %y %z}
; CHECK: %cmp.a = icmp
; CHECK-NEXT: before {{.*}} liveSet: {%cmp.a %p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%cmp.a %p2 %p3 %y %z}
; CHECK: br i1 %cmp.a
; CHECK-NEXT: before {{.*}} liveSet: {%p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p2 %p3 %y %z}

b:
; CHECK: %x = phi
; CHECK-NEXT: before {{.*}} liveSet: {%p2 %p3 %x %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p2 %p3 %x %y %z}
  %x = phi i64 [ 0, %a ]
; CHECK: %cmp.b = icmp
; CHECK-NEXT: before {{.*}} liveSet: {%cmp.b %p3 %x %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%cmp.b %p3 %x %y %z}
  %cmp.b = icmp ult i64 %p2, 2
; CHECK: br i1 %cmp.b
; CHECK-NEXT: before {{.*}} liveSet: {%p3 %x %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p3 %y %z}
  br i1 %cmp.b, label %a, label %c

c:
; CHECK: %cmp.c = icmp
; CHECK-NEXT: before {{.*}} liveSet: {%cmp.c %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%cmp.c %y %z}
  %cmp.c = icmp ult i64 %p3, 3
; CHECK: br i1 %cmp.c
; CHECK-NEXT: before {{.*}} liveSet: {%y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%y %z}
  br i1 %cmp.c, label %a, label %exit

; going back to label %a yet another time
; CHECK: %p1 = phi
; CHECK-NEXT: before {{.*}} liveSet: {%y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%y %z}
; CHECK: %p2 = phi
; CHECK-NEXT: before {{.*}} liveSet: {%y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%y %z}
; CHECK: %p3 = phi
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3}
; CHECK: %y = call
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3 %y}
; CHECK-NEXT:  after {{.*}} liveSet: {}
; CHECK: %g = add
; CHECK-NEXT: before {{.*}} liveSet: {%g}
; CHECK-NEXT:  after {{.*}} liveSet: {%g}
; CHECK: %h = add
; CHECK-NEXT: before {{.*}} liveSet: {%g %h}
; CHECK-NEXT:  after {{.*}} liveSet: {%g %h}
; CHECK: %i = sub
; CHECK-NEXT: before {{.*}} liveSet: {%g}
; CHECK-NEXT:  after {{.*}} liveSet: {%g}
; CHECK: ret i64
; CHECK-NEXT: before {{.*}} liveSet: {}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3 %y}
; CHECK: %z = add
; CHECK-NEXT: before {{.*}} liveSet: {%p1 %p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p1 %p2 %p3 %y %z}
; CHECK: %cmp.a = icmp
; CHECK-NEXT: before {{.*}} liveSet: {%cmp.a %p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%cmp.a %p2 %p3 %y %z}
; CHECK: br i1 %cmp.a
; CHECK-NEXT: before {{.*}} liveSet: {%p2 %p3 %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p2 %p3 %y %z}

; going back to label %b
; CHECK: %x = phi
; CHECK-NEXT: before {{.*}} liveSet: {%p2 %p3 %x %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p2 %p3 %x %y %z}
; CHECK: %cmp.b = icmp
; CHECK-NEXT: before {{.*}} liveSet: {%cmp.b %p3 %x %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%cmp.b %p3 %x %y %z}
; CHECK: br i1 %cmp.b
; CHECK-NEXT: before {{.*}} liveSet: {%p3 %x %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%p3 %y %z}

; going back to label %c
; CHECK: %cmp.c = icmp
; CHECK-NEXT: before {{.*}} liveSet: {%cmp.c %y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {%cmp.c %y %z}
; CHECK: br i1 %cmp.c
; CHECK-NEXT: before {{.*}} liveSet: {%y %z}
; CHECK-NEXT:  after {{.*}} liveSet: {}

exit:
; CHECK: ret void
; CHECK-NEXT: before {{.*}} liveSet: {}
; CHECK-NEXT:  after {{.*}} liveSet: {}
  ret void
}
