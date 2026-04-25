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

def FSA_PRI_RAISE_N : XFormosaPriI <0b101, "fsa.pri.raise.n">, Sched<[]>{
    bits<6> imm;
    let rs1 = 0;
    let rd = 0;
    let Inst{31-26} = 0;
    let Inst{25-20} = imm;  // the priority value to raise
}

def FSA_PRI_LOWER_N : XFormosaPriI <0b011, "fsa.pri.lower.n">, Sched<[]>{
    bits<6> imm;
    let rs1 = 0;
    let rd = 0;
    let Inst{31-26} = 0;
    let Inst{25-20} = imm;  // the priority value to lower
}

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

def uimm3_timm : PatLeaf<(timm), [{
  return isUInt<3>(N->getZExtValue());
}]>;

def as_uimm3 : SDNodeXForm<timm, [{
  return CurDAG->getTargetConstant(N->getZExtValue(), SDLoc(N), Subtarget->getXLenVT());
}]>;

// GFX
let Predicates = [HasVendorXFormosaGFX, HasStdExtZdinx, IsRV64] in {
  def FSA_ROP : RVInstR4<0b00, 0b000, OPC_CUSTOM_0,
                         (outs),
                         (ins GPR:$rs1, GPR:$rs2, GPR:$rs3),
                         "fsa.rop",
                         "$rs1, $rs2, $rs3"> {
    let rd = 0;
    let hasSideEffects = 1;
    let mayStore = 1;
    let mayLoad = 0;
  }

  def FSA_TEX : RVInstR4<0b01, 0b000, OPC_CUSTOM_0, (outs GPR:$rd),
                         (ins GPR:$rs1, GPR:$rs2, GPR:$rs3, uimm3:$tex),
                         "fsa.tex",
                         "$rd, $rs1, $rs2, $rs3, $tex"> {
    bits<3> tex;
    let Inst{14-12} = tex;
    let hasSideEffects = 1;
    let mayStore = 0;
    let mayLoad = 1;
  }

  def FSA_SWAP : RVInstR4<0b10, 0b000, OPC_CUSTOM_0, (outs GPR:$rd),
                            (ins GPR:$rs1, GPR:$rs2),
                            "fsa.swap",
                            "$rd, $rs1, $rs2"> {
    let rs3 = 0;
    let hasSideEffects = 0;
    let mayLoad = 0;
    let mayStore = 0;
  }
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
def : PatUImm6<int_riscv_fsa_pri_lower_n, FSA_PRI_LOWER_N>;
def : PatUImm6<int_riscv_fsa_pri_raise_n, FSA_PRI_RAISE_N>;

// GFX
def : Pat<(int_riscv_fsa_rop GPR:$rs1, GPR:$rs2, GPR:$rs3),
          (FSA_ROP GPR:$rs1, GPR:$rs2, GPR:$rs3)>;
def : Pat<(int_riscv_fsa_tex GPR:$rs1, GPR:$rs2, GPR:$rs3, uimm3_timm:$tex),
          (FSA_TEX GPR:$rs1, GPR:$rs2, GPR:$rs3, (as_uimm3 $tex))>;
def : Pat<(int_riscv_fsa_swap GPR:$rs1, GPR:$rs2),
          (FSA_SWAP GPR:$rs1, GPR:$rs2)>;