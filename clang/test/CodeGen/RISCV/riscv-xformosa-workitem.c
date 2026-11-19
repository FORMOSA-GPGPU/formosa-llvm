// RUN: %clang_cc1 -triple riscv64 -target-feature +xformosa -emit-llvm %s -o - \
// RUN:   | FileCheck %s
// PoCL builds its kernel library with -mcpu=formosa-gpgpu, which implies
// xformosa.
// RUN: %clang --target=riscv64 -mcpu=formosa-gpgpu -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s

// Each Formosa work-item builtin maps to its intrinsic.

// CHECK-LABEL: @local_id_x(
// CHECK: call i64 @llvm.riscv.fsa.local.id.x()
unsigned long local_id_x(void) { return __builtin_riscv_fsa_local_id_x(); }
// CHECK: declare noundef i64 @llvm.riscv.fsa.local.id.x() [[ATTR:#[0-9]+]]

// CHECK-LABEL: @local_id_y(
// CHECK: call i64 @llvm.riscv.fsa.local.id.y()
unsigned long local_id_y(void) { return __builtin_riscv_fsa_local_id_y(); }

// CHECK-LABEL: @local_id_z(
// CHECK: call i64 @llvm.riscv.fsa.local.id.z()
unsigned long local_id_z(void) { return __builtin_riscv_fsa_local_id_z(); }

// CHECK-LABEL: @local_linear_id(
// CHECK: call i64 @llvm.riscv.fsa.local.linear.id()
unsigned long local_linear_id(void) { return __builtin_riscv_fsa_local_linear_id(); }

// CHECK-LABEL: @lane_id(
// CHECK: call i64 @llvm.riscv.fsa.lane.id()
unsigned long lane_id(void) { return __builtin_riscv_fsa_lane_id(); }

// CHECK-LABEL: @sub_group_size(
// CHECK: call i64 @llvm.riscv.fsa.sub.group.size()
unsigned long sub_group_size(void) { return __builtin_riscv_fsa_sub_group_size(); }

// CHECK-LABEL: @group_id_x(
// CHECK: call i64 @llvm.riscv.fsa.group.id.x()
unsigned long group_id_x(void) { return __builtin_riscv_fsa_group_id_x(); }

// CHECK-LABEL: @group_id_y(
// CHECK: call i64 @llvm.riscv.fsa.group.id.y()
unsigned long group_id_y(void) { return __builtin_riscv_fsa_group_id_y(); }

// CHECK-LABEL: @group_id_z(
// CHECK: call i64 @llvm.riscv.fsa.group.id.z()
unsigned long group_id_z(void) { return __builtin_riscv_fsa_group_id_z(); }

// CHECK-LABEL: @local_size_x(
// CHECK: call i64 @llvm.riscv.fsa.local.size.x()
unsigned long local_size_x(void) { return __builtin_riscv_fsa_local_size_x(); }

// CHECK-LABEL: @local_size_y(
// CHECK: call i64 @llvm.riscv.fsa.local.size.y()
unsigned long local_size_y(void) { return __builtin_riscv_fsa_local_size_y(); }

// CHECK-LABEL: @local_size_z(
// CHECK: call i64 @llvm.riscv.fsa.local.size.z()
unsigned long local_size_z(void) { return __builtin_riscv_fsa_local_size_z(); }

// CHECK-LABEL: @global_offset_x(
// CHECK: call i64 @llvm.riscv.fsa.global.offset.x()
unsigned long global_offset_x(void) { return __builtin_riscv_fsa_global_offset_x(); }

// CHECK-LABEL: @global_offset_y(
// CHECK: call i64 @llvm.riscv.fsa.global.offset.y()
unsigned long global_offset_y(void) { return __builtin_riscv_fsa_global_offset_y(); }

// CHECK-LABEL: @global_offset_z(
// CHECK: call i64 @llvm.riscv.fsa.global.offset.z()
unsigned long global_offset_z(void) { return __builtin_riscv_fsa_global_offset_z(); }

// CHECK: attributes [[ATTR]] = { {{.*}}speculatable{{.*}}memory(none){{.*}} }
