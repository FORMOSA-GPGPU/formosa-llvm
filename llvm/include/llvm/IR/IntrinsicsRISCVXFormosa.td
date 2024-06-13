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

} // TargetPrefix = "riscv"
