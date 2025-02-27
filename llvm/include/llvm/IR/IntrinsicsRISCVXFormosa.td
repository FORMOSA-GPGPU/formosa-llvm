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

  class RISCVXFormosaCSRIntrinsics :
    Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrNoFree]>;

  multiclass RISCVXFormosaCSRIntrinsicsNamed<string IntrinsicName> {
    def _x : ClangBuiltin<!strconcat("__builtin_riscv_fsa_", IntrinsicName, "_x")>,
             RISCVXFormosaCSRIntrinsics;
    def _y : ClangBuiltin<!strconcat("__builtin_riscv_fsa_", IntrinsicName, "_y")>,
             RISCVXFormosaCSRIntrinsics;
    def _z : ClangBuiltin<!strconcat("__builtin_riscv_fsa_", IntrinsicName, "_z")>,
             RISCVXFormosaCSRIntrinsics;
  }

  defm int_riscv_fsa_local_id : RISCVXFormosaCSRIntrinsicsNamed<"local_id">;
  defm int_riscv_fsa_local_size : RISCVXFormosaCSRIntrinsicsNamed<"local_size">;
  defm int_riscv_fsa_global_size : RISCVXFormosaCSRIntrinsicsNamed<"global_size">;
  defm int_riscv_fsa_global_offset : RISCVXFormosaCSRIntrinsicsNamed<"global_offset">;
  defm int_riscv_fsa_group_id : RISCVXFormosaCSRIntrinsicsNamed<"group_id">;

  def  int_riscv_fsa_thread_id : ClangBuiltin<"__builtin_riscv_fsa_thread_id">,
    Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrNoFree]>;

  def  int_riscv_fsa_thread_status : ClangBuiltin<"__builtin_riscv_fsa_thread_status">,
    Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrNoFree]>;

  def int_riscv_fsa_xdim : ClangBuiltin<"__builtin_riscv_fsa_xdim">,
    Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrNoFree]>;
} // TargetPrefix = "riscv"
