let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class XFormosaPri<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1, (outs), (ins),
              opcodestr, "">;

def FSA_PRI_RAISE : XFormosaPri<0b0000000, 0b000, "fsa.pri.raise">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

def FSA_PRI_LOWER : XFormosaPri<0b0100000, 0b000, "fsa.pri.lower">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

def FSA_PRI_RESET : XFormosaPri<0b0000001, 0b000, "fsa.pri.reset">, Sched<[]>{
    let rs1 = 0;
    let rs2 = 0;
    let rd = 0;
}

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class XFormosaBar<bits<3> funct3, string opcodestr> :
    RVInstI<funct3, OPC_CUSTOM_1, (outs), (ins uimm5:$bid, uimm6:$wc), opcodestr, "$bid, $wc">;

def FSA_BAR : XFormosaBar<0b001, "fsa.bar"> , Sched<[]>{
    bits<6> wc;  // warp count
    bits<5> bid; // barrier id
    let rd = 0;
    let Inst{31-26} = 0;
    let Inst{25-20} = wc;
    let rs1 = bid;
}
