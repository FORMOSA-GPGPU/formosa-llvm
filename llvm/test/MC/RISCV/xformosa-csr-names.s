# RUN: llvm-mc %s -triple=riscv64 -mattr=+xformosa -M no-aliases -show-encoding \
# RUN:     | FileCheck -check-prefixes=CHECK-INST,CHECK-ENC %s
# RUN: llvm-mc -filetype=obj -triple riscv64 -mattr=+xformosa < %s \
# RUN:     | llvm-objdump -d --mattr=+xformosa - \
# RUN:     | FileCheck -check-prefix=CHECK-INST-ALIAS %s
# RUN: llvm-mc -filetype=obj -triple riscv64 -mattr=+xformosa < %s \
# RUN:     | llvm-objdump --no-print-imm-hex -d - \
# RUN:     | FileCheck -check-prefix=CHECK-NOFEAT %s
# RUN: not llvm-mc -triple riscv64 %s 2>&1 \
# RUN:     | FileCheck -check-prefix=CHECK-ERR %s

# Formosa work-item CSRs (machine-mode custom read/write, 0xBC0-0xBFF). The
# names need the xformosa extension; without it the CSRs are only numbers.

# CHECK-ERR: error: system register 'xsgsize' requires 'xformosa' to be enabled

# xsgsize
# CHECK-INST: csrrs t1, xsgsize, zero
# CHECK-ENC: encoding: [0x73,0x23,0x00,0xbc]
# CHECK-INST-ALIAS: csrr t1, xsgsize
# CHECK-NOFEAT: csrr t1, 3008
csrrs t1, xsgsize, zero

# xlocalid_x
# CHECK-INST: csrrs t1, xlocalid_x, zero
# CHECK-ENC: encoding: [0x73,0x23,0x00,0xbd]
# CHECK-INST-ALIAS: csrr t1, xlocalid_x
# CHECK-NOFEAT: csrr t1, 3024
csrrs t1, xlocalid_x, zero

# xlocalid_y
# CHECK-INST: csrrs t1, xlocalid_y, zero
# CHECK-ENC: encoding: [0x73,0x23,0x10,0xbd]
# CHECK-INST-ALIAS: csrr t1, xlocalid_y
# CHECK-NOFEAT: csrr t1, 3025
csrrs t1, xlocalid_y, zero

# xlocalid_z
# CHECK-INST: csrrs t1, xlocalid_z, zero
# CHECK-ENC: encoding: [0x73,0x23,0x20,0xbd]
# CHECK-INST-ALIAS: csrr t1, xlocalid_z
# CHECK-NOFEAT: csrr t1, 3026
csrrs t1, xlocalid_z, zero

# xgroupid_x
# CHECK-INST: csrrs t1, xgroupid_x, zero
# CHECK-ENC: encoding: [0x73,0x23,0x00,0xbf]
# CHECK-INST-ALIAS: csrr t1, xgroupid_x
# CHECK-NOFEAT: csrr t1, 3056
csrrs t1, xgroupid_x, zero

# xgroupid_y
# CHECK-INST: csrrs t1, xgroupid_y, zero
# CHECK-ENC: encoding: [0x73,0x23,0x10,0xbf]
# CHECK-INST-ALIAS: csrr t1, xgroupid_y
# CHECK-NOFEAT: csrr t1, 3057
csrrs t1, xgroupid_y, zero

# xgroupid_z
# CHECK-INST: csrrs t1, xgroupid_z, zero
# CHECK-ENC: encoding: [0x73,0x23,0x20,0xbf]
# CHECK-INST-ALIAS: csrr t1, xgroupid_z
# CHECK-NOFEAT: csrr t1, 3058
csrrs t1, xgroupid_z, zero

# xlocalsize_x
# CHECK-INST: csrrs t1, xlocalsize_x, zero
# CHECK-ENC: encoding: [0x73,0x23,0x40,0xbf]
# CHECK-INST-ALIAS: csrr t1, xlocalsize_x
# CHECK-NOFEAT: csrr t1, 3060
csrrs t1, xlocalsize_x, zero

# xlocalsize_y
# CHECK-INST: csrrs t1, xlocalsize_y, zero
# CHECK-ENC: encoding: [0x73,0x23,0x50,0xbf]
# CHECK-INST-ALIAS: csrr t1, xlocalsize_y
# CHECK-NOFEAT: csrr t1, 3061
csrrs t1, xlocalsize_y, zero

# xlocalsize_z
# CHECK-INST: csrrs t1, xlocalsize_z, zero
# CHECK-ENC: encoding: [0x73,0x23,0x60,0xbf]
# CHECK-INST-ALIAS: csrr t1, xlocalsize_z
# CHECK-NOFEAT: csrr t1, 3062
csrrs t1, xlocalsize_z, zero

# xglobaloffset_x
# CHECK-INST: csrrs t1, xglobaloffset_x, zero
# CHECK-ENC: encoding: [0x73,0x23,0x80,0xbf]
# CHECK-INST-ALIAS: csrr t1, xglobaloffset_x
# CHECK-NOFEAT: csrr t1, 3064
csrrs t1, xglobaloffset_x, zero

# xglobaloffset_y
# CHECK-INST: csrrs t1, xglobaloffset_y, zero
# CHECK-ENC: encoding: [0x73,0x23,0x90,0xbf]
# CHECK-INST-ALIAS: csrr t1, xglobaloffset_y
# CHECK-NOFEAT: csrr t1, 3065
csrrs t1, xglobaloffset_y, zero

# xglobaloffset_z
# CHECK-INST: csrrs t1, xglobaloffset_z, zero
# CHECK-ENC: encoding: [0x73,0x23,0xa0,0xbf]
# CHECK-INST-ALIAS: csrr t1, xglobaloffset_z
# CHECK-NOFEAT: csrr t1, 3066
csrrs t1, xglobaloffset_z, zero

# Writes, as the PoCL <kernel>_wrapper does.
# CHECK-INST: csrrw zero, xlocalid_x, a0
# CHECK-ENC: encoding: [0x73,0x10,0x05,0xbd]
# CHECK-INST-ALIAS: csrw xlocalid_x, a0
# CHECK-NOFEAT: csrw 3024, a0
csrrw zero, xlocalid_x, a0
