#include <clc/clc.h>

_CLC_DEF _CLC_OVERLOAD size_t get_num_groups(uint dim) {
    size_t base, rest;
    switch (dim) {
    case 0:
        base = __builtin_riscv_fsa_global_size_x() 
            / __builtin_riscv_fsa_local_size_x();
        rest = __builtin_riscv_fsa_global_size_x() 
            % __builtin_riscv_fsa_local_size_x() ? 0: 1;
        return base + rest;
    case 1:
        base = __builtin_riscv_fsa_global_size_y() 
            / __builtin_riscv_fsa_local_size_y();
        rest = __builtin_riscv_fsa_global_size_y() 
            % __builtin_riscv_fsa_local_size_y() ? 0: 1;
        return base + rest;
    case 2:
        base = __builtin_riscv_fsa_global_size_z() 
            / __builtin_riscv_fsa_local_size_z();
        rest = __builtin_riscv_fsa_global_size_z() 
            % __builtin_riscv_fsa_local_size_z() ? 0: 1;
        return base + rest;
    default:
        return 1;
  }
}