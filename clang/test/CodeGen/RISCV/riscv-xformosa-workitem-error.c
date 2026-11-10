// RUN: %clang_cc1 -triple riscv64 -emit-llvm-only -verify %s
// RUN: %clang_cc1 -triple riscv64 -target-feature +xformosapri -emit-llvm-only -verify %s
// RUN: %clang_cc1 -triple riscv32 -target-feature +xformosa -emit-llvm-only -verify %s

// The work-item builtins need the xformosa extension on RV64.

unsigned long local_id_x(void) {
  return __builtin_riscv_fsa_local_id_x(); // expected-error {{'__builtin_riscv_fsa_local_id_x' needs target feature xformosa,64bit}}
}
