; RUN: opt -mtriple=riscv64 -mattr=+xformosa -passes='print<uniformity>' \
; RUN:   -disable-output %s 2>&1 | FileCheck %s

; Per-lane work-item values are divergent. Per-warp and per-work-group values
; are the same in every lane of a warp, so they are uniform.

; CHECK-LABEL: UniformityInfo for function 'workitem':
; CHECK: DIVERGENT: %lid.x = call i64 @llvm.riscv.fsa.local.id.x()
; CHECK: DIVERGENT: %lid.y = call i64 @llvm.riscv.fsa.local.id.y()
; CHECK: DIVERGENT: %lid.z = call i64 @llvm.riscv.fsa.local.id.z()
; CHECK: DIVERGENT: %llid = call i64 @llvm.riscv.fsa.local.linear.id()
; CHECK: DIVERGENT: %lane = call i64 @llvm.riscv.fsa.lane.id()
; CHECK: {{^ +}}%sgsize = call i64 @llvm.riscv.fsa.sub.group.size()
; CHECK: {{^ +}}%gid.x = call i64 @llvm.riscv.fsa.group.id.x()
; CHECK: {{^ +}}%gid.y = call i64 @llvm.riscv.fsa.group.id.y()
; CHECK: {{^ +}}%gid.z = call i64 @llvm.riscv.fsa.group.id.z()
; CHECK: {{^ +}}%size.x = call i64 @llvm.riscv.fsa.local.size.x()
; CHECK: {{^ +}}%size.y = call i64 @llvm.riscv.fsa.local.size.y()
; CHECK: {{^ +}}%size.z = call i64 @llvm.riscv.fsa.local.size.z()
; CHECK: {{^ +}}%off.x = call i64 @llvm.riscv.fsa.global.offset.x()
; CHECK: {{^ +}}%off.y = call i64 @llvm.riscv.fsa.global.offset.y()
; CHECK: {{^ +}}%off.z = call i64 @llvm.riscv.fsa.global.offset.z()
; CHECK: {{^ +}}%uniform = add i64 %gid.x, %size.x
; CHECK: DIVERGENT: %divergent = add i64 %uniform, %lid.x

define void @workitem(ptr %out) {
  %lid.x = call i64 @llvm.riscv.fsa.local.id.x()
  %lid.y = call i64 @llvm.riscv.fsa.local.id.y()
  %lid.z = call i64 @llvm.riscv.fsa.local.id.z()
  %llid = call i64 @llvm.riscv.fsa.local.linear.id()
  %lane = call i64 @llvm.riscv.fsa.lane.id()
  %sgsize = call i64 @llvm.riscv.fsa.sub.group.size()
  %gid.x = call i64 @llvm.riscv.fsa.group.id.x()
  %gid.y = call i64 @llvm.riscv.fsa.group.id.y()
  %gid.z = call i64 @llvm.riscv.fsa.group.id.z()
  %size.x = call i64 @llvm.riscv.fsa.local.size.x()
  %size.y = call i64 @llvm.riscv.fsa.local.size.y()
  %size.z = call i64 @llvm.riscv.fsa.local.size.z()
  %off.x = call i64 @llvm.riscv.fsa.global.offset.x()
  %off.y = call i64 @llvm.riscv.fsa.global.offset.y()
  %off.z = call i64 @llvm.riscv.fsa.global.offset.z()
  %uniform = add i64 %gid.x, %size.x
  %divergent = add i64 %uniform, %lid.x
  store volatile i64 %lid.y, ptr %out
  store volatile i64 %lid.z, ptr %out
  store volatile i64 %llid, ptr %out
  store volatile i64 %lane, ptr %out
  store volatile i64 %sgsize, ptr %out
  store volatile i64 %gid.y, ptr %out
  store volatile i64 %gid.z, ptr %out
  store volatile i64 %size.y, ptr %out
  store volatile i64 %size.z, ptr %out
  store volatile i64 %off.x, ptr %out
  store volatile i64 %off.y, ptr %out
  store volatile i64 %off.z, ptr %out
  store volatile i64 %divergent, ptr %out
  ret void
}
