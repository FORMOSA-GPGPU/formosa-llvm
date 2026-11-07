; RUN: llc -mtriple=riscv64 -mcpu=formosa-gpgpu -verify-machineinstrs < %s | FileCheck %s

; fsa.bar is a workgroup barrier: memory accesses must not be moved across it.
; Here the loaded value is only used in a conditional successor, which makes
; MachineSink want to sink the load past the barrier into %then. That is the
; race seen in dlprim's softmax reduction (myval = my_reduce[0]; barrier;
; my_reduce[0] = ... by another warp).

define void @load_before_bar(ptr %p, ptr %out, i1 %c) {
; CHECK-LABEL: load_before_bar:
; CHECK:       {{l[dw]}} [[V:[a-z0-9]+]], 0(a0)
; CHECK:       fsa.bar 0, 1
; CHECK:       # %then
; CHECK-NOT:   {{l[dw]}} {{.*}}0(a0)
; CHECK:       {{s[dw]}} [[V]], 0(a1)
entry:
  %v = load i64, ptr %p, align 8
  call void @llvm.riscv.fsa.barrier(i64 0, i64 1)
  br i1 %c, label %then, label %exit

then:
  store i64 %v, ptr %out, align 8
  br label %exit

exit:
  ret void
}

; A store before the barrier must stay before it as well.
define void @store_before_bar(ptr %p, i64 %x) {
; CHECK-LABEL: store_before_bar:
; CHECK:       sd a1, 0(a0)
; CHECK-NEXT:  fsa.bar 0, 1
entry:
  store i64 %x, ptr %p, align 8
  call void @llvm.riscv.fsa.barrier(i64 0, i64 1)
  ret void
}

declare void @llvm.riscv.fsa.barrier(i64 immarg, i64 immarg)
