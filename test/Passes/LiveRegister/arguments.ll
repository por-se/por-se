; RUN: %opt -load %kleelibdir/LiveRegister.so -live-register -analyze %s 2>&1 | FileCheck %s

define i32 @test(i32 %x, i32 %y) {
entry:
; CHECK-LABEL: entry:
; CHECK-NEXT: %x.addr = {{.*}} ; live = {%x, %x.addr, %y}
; CHECK-NEXT: %y.addr = {{.*}} ; live = {%x, %x.addr, %y, %y.addr}
; CHECK-NEXT: %z = {{.*}} ; live = {%x, %x.addr, %y, %y.addr, %z}
; CHECK-NEXT: store i32 %x, {{.*}} ; live = {%x.addr, %y, %y.addr, %z}
; CHECK-NEXT: store i32 %y, {{.*}} ; live = {%x.addr, %y.addr, %z}
; CHECK-NEXT: %0 = {{.*}} ; live = {%y.addr, %z, %0}
; CHECK-NEXT: %mul = {{.*}} ; live = {%mul, %y.addr, %z}
; CHECK-NEXT: store i32 %mul, {{.*}} ; live = {%y.addr, %z}
; CHECK-NEXT: %1 = {{.*}} ; live = {%y.addr, %z, %1}
; CHECK-NEXT: %inc = {{.*}} ; live = {%inc, %y.addr, %z}
; CHECK-NEXT: store i32 %inc, {{.*}} ; live = {%y.addr, %z}
; CHECK-NEXT: %2 = {{.*}} ; live = {%y.addr, %2}
; CHECK-NEXT: %3 = {{.*}} ; live = {%3, %2}
; CHECK-NEXT: %add = {{.*}} ; live = {%add}
; CHECK-NEXT: ret {{.*}} ; live = {}
  %x.addr = alloca i32
  %y.addr = alloca i32
  %z = alloca i32
  store i32 %x, i32* %x.addr
  store i32 %y, i32* %y.addr
  %0 = load i32, i32* %x.addr
  %mul = mul nsw i32 %0, 3
  store i32 %mul, i32* %z
  %1 = load i32, i32* %z
  %inc = add nsw i32 %1, 1
  store i32 %inc, i32* %z
  %2 = load i32, i32* %z
  %3 = load i32, i32* %y.addr
  %add = add nsw i32 %2, %3
  ret i32 %add
}
