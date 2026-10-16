// REQUIRES: riscv-registered-target
// RUN: %clang --target=riscv64-unknown-elf -mcpu=formosa-gpgpu -O2 -S %s -o - \
// RUN:   | FileCheck %s --check-prefixes=CHECK,RV64 \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_ \
// RUN:   --implicit-check-not=lr.w --implicit-check-not=lr.d \
// RUN:   --implicit-check-not=sc.w --implicit-check-not=sc.d
// RUN: %clang --target=riscv64-unknown-elf -march=rv64i_zacas -O2 -S %s -o - \
// RUN:   | FileCheck %s --check-prefixes=CHECK,RV64 \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_ \
// RUN:   --implicit-check-not=lr.w --implicit-check-not=lr.d \
// RUN:   --implicit-check-not=sc.w --implicit-check-not=sc.d
// RUN: %clang --target=riscv32-unknown-elf -march=rv32i_zacas -O2 -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=CHECK \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_ \
// RUN:   --implicit-check-not=lr.w --implicit-check-not=sc.w

// Check the complete frontend/backend path, including CPU-default features.
// Subword operations must use widened AMOCAS.W loops without requiring LR/SC.
#define CAS(T, N)                                                               \
  T cas_##N(T *p, T old, T value) {                                              \
    __atomic_compare_exchange_n(p, &old, value, 0, __ATOMIC_SEQ_CST,              \
                                __ATOMIC_SEQ_CST);                             \
    return old;                                                                \
  }

// CHECK-LABEL: cas_8:
// CHECK: amocas.w.aqrl
CAS(unsigned char, 8)
// CHECK-LABEL: cas_16:
// CHECK: amocas.w.aqrl
CAS(unsigned short, 16)
// CHECK-LABEL: cas_32:
// CHECK: amocas.w.aqrl
CAS(unsigned int, 32)
// RV64-LABEL: cas_64:
// RV64: amocas.d.aqrl
#if __riscv_xlen == 64
CAS(unsigned long, 64)
#endif

// CHECK-LABEL: add_8:
// CHECK: amocas.w.aqrl
unsigned char add_8(unsigned char *p, unsigned char value) {
  return __atomic_fetch_add(p, value, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: sub_32:
// CHECK: amoadd.w.aqrl
unsigned int sub_32(unsigned int *p, unsigned int value) {
  return __atomic_fetch_sub(p, value, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: nand_16:
// CHECK: amocas.w.aqrl
unsigned short nand_16(unsigned short *p, unsigned short value) {
  return __atomic_fetch_nand(p, value, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: nand_32:
// CHECK: amocas.w.aqrl
unsigned int nand_32(unsigned int *p, unsigned int value) {
  return __atomic_fetch_nand(p, value, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: load_32:
// CHECK: fence rw, rw
// CHECK: lw
// CHECK: fence r, rw
unsigned int load_32(unsigned int *p) {
  return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: store_32:
// CHECK: fence rw, w
// CHECK: sw
void store_32(unsigned int *p, unsigned int value) {
  __atomic_store_n(p, value, __ATOMIC_SEQ_CST);
}
