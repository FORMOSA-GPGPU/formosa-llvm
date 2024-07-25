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

  def int_riscv_fsa_pri_base : ClangBuiltin<"__builtin_riscv_fsa_pri_base">,
                               RISCVXFormosaPriIntrinsics;

  def int_riscv_fsa_pri_set : ClangBuiltin<"__builtin_riscv_fsa_pri_set">,
    Intrinsic<[], [llvm_i64_ty],
              [IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback,
               IntrNoFree]>;

  def int_riscv_fsa_barrier : ClangBuiltin<"__builtin_riscv_fsa_barrier">,
    Intrinsic<[], [llvm_i64_ty, llvm_i64_ty],
              [IntrNoMem, IntrHasSideEffects, IntrConvergent,
               IntrWillReturn, IntrNoCallback, IntrNoFree]>;
} // TargetPrefix = "riscv"
