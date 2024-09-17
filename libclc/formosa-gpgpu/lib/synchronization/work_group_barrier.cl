#include <clc/clc.h>

// work_group_barrier is alias of barrier after opencl_2.0
_CLC_DEF _CLC_OVERLOAD void work_group_barrier(cl_mem_fence_flags flags) {
   // The 'flags' argument is not used here because the FSA barrier 
   // instruction does not distinguish between global and local memory
   // fences.
   __builtin_riscv_fsa_barrier(0, 0);
}