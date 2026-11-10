//===- IntrinsicsRISCVXFSA.td - FORMOSA intrinsics -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the FORMOSA GPGPU vendor intrinsics for RISC-V.
//
//===----------------------------------------------------------------------===//


let TargetPrefix = "riscv" in {
  
  class RISCVXFormosaPriIntrinsics :
    Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects, IntrWillReturn, 
                       IntrNoCallback, IntrNoFree]>;

  def int_riscv_fsa_pri_raise : ClangBuiltin<"__builtin_riscv_fsa_pri_raise">,
                                RISCVXFormosaPriIntrinsics;

  def int_riscv_fsa_pri_lower : ClangBuiltin<"__builtin_riscv_fsa_pri_lower">,
                                RISCVXFormosaPriIntrinsics;

  def int_riscv_fsa_pri_lower_n : ClangBuiltin<"__builtin_riscv_fsa_pri_lower_n">,
    Intrinsic<[], [llvm_i64_ty],
              [IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback,
              IntrNoFree]>;

  def int_riscv_fsa_pri_raise_n : ClangBuiltin<"__builtin_riscv_fsa_pri_raise_n">,
    Intrinsic<[], [llvm_i64_ty],
              [IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback,
               IntrNoFree]>;

  def int_riscv_fsa_pri_reset : ClangBuiltin<"__builtin_riscv_fsa_pri_reset">,
                                RISCVXFormosaPriIntrinsics;

  def int_riscv_fsa_pri_set : ClangBuiltin<"__builtin_riscv_fsa_pri_set">,
    Intrinsic<[], [llvm_i64_ty],
              [IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback,
               IntrNoFree]>;

  def int_riscv_fsa_barrier : ClangBuiltin<"__builtin_riscv_fsa_barrier">,
    Intrinsic<[], [llvm_i64_ty, llvm_i64_ty],
              [IntrNoMem, IntrHasSideEffects, IntrConvergent,
               IntrWillReturn, IntrNoCallback, IntrNoFree]>;

  class RISCVXFormosaWorkItemIntrinsic<string name>
    : DefaultAttrsIntrinsic<[llvm_i64_ty], [],
                             [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>,
      ClangBuiltin<!strconcat("__builtin_riscv_fsa_", name)>;

  multiclass RISCVXFormosaWorkItemIntrinsicXYZ<string name> {
    def _x : RISCVXFormosaWorkItemIntrinsic<!strconcat(name, "_x")>;
    def _y : RISCVXFormosaWorkItemIntrinsic<!strconcat(name, "_y")>;
    def _z : RISCVXFormosaWorkItemIntrinsic<!strconcat(name, "_z")>;
  }

  // per lane
  defm int_riscv_fsa_local_id : RISCVXFormosaWorkItemIntrinsicXYZ<"local_id">;
  def  int_riscv_fsa_local_linear_id : RISCVXFormosaWorkItemIntrinsic<"local_linear_id">;
  def  int_riscv_fsa_lane_id : RISCVXFormosaWorkItemIntrinsic<"lane_id">;

  // per warp
  def  int_riscv_fsa_sub_group_size : RISCVXFormosaWorkItemIntrinsic<"sub_group_size">;

  // per work-group
  defm int_riscv_fsa_group_id : RISCVXFormosaWorkItemIntrinsicXYZ<"group_id">;
  defm int_riscv_fsa_local_size : RISCVXFormosaWorkItemIntrinsicXYZ<"local_size">;
  defm int_riscv_fsa_global_offset : RISCVXFormosaWorkItemIntrinsicXYZ<"global_offset">;

  def int_riscv_fsa_rop : ClangBuiltin<"__builtin_riscv_fsa_rop">, 
    Intrinsic<[], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty],
      [IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrWriteMem]>;

  def int_riscv_fsa_tex : ClangBuiltin<"__builtin_riscv_fsa_tex">,
    Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty, llvm_i32_ty],
      [IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrReadMem, ImmArg<ArgIndex<3>>]>;
  def int_riscv_fsa_swap : ClangBuiltin<"__builtin_riscv_fsa_swap">,
    Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
      [IntrWillReturn, IntrNoMem, IntrSpeculatable, IntrNoCallback]>;
} // TargetPrefix = "riscv"
