let Predicates = [HasVendorXFormosaPri, IsRV64],
    hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class XFormosaPri<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1, (outs), (ins),
              opcodestr, "">;

// Instructions for FSA thread priority control
def FSA_PRI_RAISE : XFormosaPri<0b0000000, 0b000, "fsa.pri.raise.t">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

def FSA_PRI_LOWER : XFormosaPri<0b0100000, 0b000, "fsa.pri.lower.t">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

// Instructions for FSA function priority control
def FSA_PRI_RAISE_F : XFormosaPri<0b0000000, 0b010, "fsa.pri.raise.f">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

def FSA_PRI_LOWER_F : XFormosaPri<0b0100000, 0b010, "fsa.pri.lower.f">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

def FSA_PRI_RESET : XFormosaPri<0b0000001, 0b000, "fsa.pri.reset">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

let Predicates = [HasVendorXFormosaPri, IsRV64],
    hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class XFormosaPriI<bits<3> funct3, string opcodestr>
    : RVInstI<funct3, OPC_CUSTOM_1, (outs), (ins uimm6:$imm), opcodestr, "$imm">;

def FSA_PRI_SET : XFormosaPriI<0b100, "fsa.pri.set.t">, Sched<[]>{
    bits<6> imm;
    let rs1 = 0;
    let rd = 0;
    let Inst{31-26} = 0;
    let Inst{25-20} = imm;  // the priority value to set
}

let Predicates = [HasVendorXFormosaBar, IsRV64],
    hasSideEffects = 1, mayLoad = 0, mayStore = 0, isConvergent = 1 in
class XFormosaBar<bits<3> funct3, string opcodestr> :
    RVInstI<funct3, OPC_CUSTOM_1, (outs),
            (ins uimm6:$bid, uimm6:$wc), opcodestr, "$bid, $wc">;

def FSA_BAR : XFormosaBar<0b001, "fsa.bar"> , Sched<[]>{
    bits<6> wc;  // warp count
    bits<6> bid; // barrier id
    let rs1 = 0;
    let rd = 0;
    let Inst{31-26} = bid;
    let Inst{25-20} = wc;
}

// Pattern matching for FSA instructions

class PatUImm6UImm6<Intrinsic intrinsic, RVInstI inst>
    : Pat<(intrinsic uimm6:$imm1, uimm6:$imm2),
          (inst uimm6:$imm1, uimm6:$imm2)>;

class PatUImm6<Intrinsic intrinsic, RVInstI inst>
    : Pat<(intrinsic uimm6:$imm), (inst uimm6:$imm)>;

def : Pat<(int_riscv_fsa_pri_raise), (FSA_PRI_RAISE)>;
def : Pat<(int_riscv_fsa_pri_lower), (FSA_PRI_LOWER)>;
def : Pat<(int_riscv_fsa_pri_reset), (FSA_PRI_RESET)>;

def : PatUImm6UImm6<int_riscv_fsa_barrier, FSA_BAR>;
def : PatUImm6<int_riscv_fsa_pri_set, FSA_PRI_SET>;

def : Pat<(i64 (int_riscv_fsa_local_id_x)),      (CSRRS FSA_XCLLX.Encoding,    (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_local_id_y)),      (CSRRS FSA_XCLLY.Encoding,    (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_local_id_z)),      (CSRRS FSA_XCLLZ.Encoding,    (XLenVT X0))>;

def : Pat<(i64 (int_riscv_fsa_local_size_x)),    (CSRRS FSA_XCLSX.Encoding,    (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_local_size_y)),    (CSRRS FSA_XCLSY.Encoding,    (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_local_size_z)),    (CSRRS FSA_XCLSZ.Encoding,    (XLenVT X0))>;

def : Pat<(i64 (int_riscv_fsa_global_size_x)),   (CSRRS FSA_XCLGSX.Encoding,   (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_global_size_y)),   (CSRRS FSA_XCLGSY.Encoding,   (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_global_size_z)),   (CSRRS FSA_XCLGSZ.Encoding,   (XLenVT X0))>;

def : Pat<(i64 (int_riscv_fsa_global_offset_x)), (CSRRS FSA_XCLGOFFX.Encoding, (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_global_offset_y)), (CSRRS FSA_XCLGOFFY.Encoding, (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_global_offset_z)), (CSRRS FSA_XCLGOFFZ.Encoding, (XLenVT X0))>;

def : Pat<(i64 (int_riscv_fsa_group_id_x)),      (CSRRS FSA_XCLWX.Encoding,    (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_group_id_y)),      (CSRRS FSA_XCLWY.Encoding,    (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_group_id_z)),      (CSRRS FSA_XCLWZ.Encoding,    (XLenVT X0))>;

def : Pat<(i64 (int_riscv_fsa_thread_id)),       (CSRRS FSA_XTID.Encoding,     (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_thread_status)),   (CSRRS FSA_XTSTATUS.Encoding, (XLenVT X0))>;
def : Pat<(i64 (int_riscv_fsa_xdim)),            (CSRRS FSA_XDIM.Encoding,     (XLenVT X0))>;
