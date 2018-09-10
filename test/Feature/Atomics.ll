; RUN: %llvmas %s -o=%t.bc
; RUN: rm -rf %t.klee-out
; RUN: %klee -exit-on-error --output-dir=%t.klee-out -disable-opt %t.bc
; ModuleID = 'atomics.cpp'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nounwind uwtable
define i32 @main(i32 %argc, i8** %args) #0 {
entry:
  %a = alloca i32, align 4

  atomicrmw xchg i32* %a, i32 5 acquire
; a = 5
  atomicrmw add i32* %a, i32 5 acquire
; a = 10
  atomicrmw sub i32* %a, i32 3 acquire
; a = 7
  atomicrmw and i32* %a, i32 13 acquire
; a = 5
  atomicrmw or i32* %a, i32 8 acquire
; a = 13
  atomicrmw xor i32* %a, i32 3 acquire
; a = 14
  atomicrmw max i32* %a, i32 12 acquire
; a = 14
  atomicrmw min i32* %a, i32 12 acquire
; a = 12

  %8 = load i32, i32* %a
  %cond = icmp eq i32 %0, 12
  br i1 %cond, label %assert.cont1, label %assert.block
assert.cont1:
  ret i32 0
assert.block:
  unreachable
}

