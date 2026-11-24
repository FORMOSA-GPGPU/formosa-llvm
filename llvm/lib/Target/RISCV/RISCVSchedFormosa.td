def FormosaModel : SchedMachineModel {
    // CompleteModel = 1 means the model in this file is completely well-defined and ask llvm
    // Do not use any other possible model assumption when scheduling
    let CompleteModel = 0;  // Whether all instructions is well-defined in this model

    let IssueWidth = 1;

    // Need to check following:
    // MicroOpBufferSize = 1 means that instructions will always be added
    // the ready queue when they become available.  This exposes them
    // to the register pressure analysis.

    // let FullInstRWOverlapCheck = 1;
    let EnableIntervals = 1;
    let MicroOpBufferSize = 1;
    // let LoopMicroOpBufferSize = -1;

    // profile if this is needed
    let PostRAScheduler = 1;

    // TODO: profile for best penalty value
    int MispredictPenalty = 40; // We won't predict, we stall until cond resolved instead

    let UnsupportedFeatures = [HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                                HasStdExtZcmt, HasStdExtZknd, HasStdExtZkne,
                                HasStdExtZknh, HasStdExtZksed, HasStdExtZksh,
                                HasStdExtZkr, HasVInstructions, HasVInstructionsI64];
}

// BufferSize : refer to kInstrQueue
let BufferSize = 2 in {
    def FormosaUnitIALU       : ProcResource<1>; // ALU: int, branch/jump, CSR
    def FormosaUnitIMul       : ProcResource<1>; // MDU: mul, div, rem
    def FormosaUnitFPALU      : ProcResource<1>; // FPU: every FP op incl. div/sqrt
}

// possible more BufferSize ?
let BufferSize = 8 in {
    def FormosaUnitMem        : ProcResource<1>; // Load/Store
}

//===----------------------------------------------------------------------===//

let SchedModel = FormosaModel in {

// Branching is dispatched to IALU
let Latency = 14 in {
def : WriteRes<WriteJmp, [FormosaUnitIALU]>;
def : WriteRes<WriteJal, [FormosaUnitIALU]>;
def : WriteRes<WriteJalr, [FormosaUnitIALU]>;

// Integer arithmetic and logic
def : WriteRes<WriteIALU32, [FormosaUnitIALU]>;
def : WriteRes<WriteIALU, [FormosaUnitIALU]>;
def : WriteRes<WriteShiftImm32, [FormosaUnitIALU]>;
def : WriteRes<WriteShiftImm, [FormosaUnitIALU]>;
def : WriteRes<WriteShiftReg32, [FormosaUnitIALU]>;
def : WriteRes<WriteShiftReg, [FormosaUnitIALU]>;
}

// Integer MUL/DIV/REM
let Latency = 18 in {
def : WriteRes<WriteIMul, [FormosaUnitIMul]>;
def : WriteRes<WriteIMul32, [FormosaUnitIMul]>;
def : WriteRes<WriteIDiv32, [FormosaUnitIMul]>;
def : WriteRes<WriteIDiv, [FormosaUnitIMul]>;
def : WriteRes<WriteIRem32, [FormosaUnitIMul]>;
def : WriteRes<WriteIRem, [FormosaUnitIMul]>;
}

// Memory
let Latency = 4 in {
def : WriteRes<WriteSTB, [FormosaUnitMem]>;
def : WriteRes<WriteSTH, [FormosaUnitMem]>;
def : WriteRes<WriteSTW, [FormosaUnitMem]>;
def : WriteRes<WriteSTD, [FormosaUnitMem]>;
def : WriteRes<WriteFST32, [FormosaUnitMem]>;
def : WriteRes<WriteFST64, [FormosaUnitMem]>;
}

let Latency = 15 in {
def : WriteRes<WriteLDB, [FormosaUnitMem]>;
def : WriteRes<WriteLDH, [FormosaUnitMem]>;
}

let Latency = 15 in {
def : WriteRes<WriteLDW, [FormosaUnitMem]>;
def : WriteRes<WriteLDD, [FormosaUnitMem]>;
def : WriteRes<WriteFLD32, [FormosaUnitMem]>;
def : WriteRes<WriteFLD64, [FormosaUnitMem]>;

// Atomic memory: 15 = fastest L1D-hit load-to-use (gemm trace). AMOs bypass L1
// and were not measured, so this is a lower bound for them.
def : WriteRes<WriteAtomicW, [FormosaUnitMem]>;
def : WriteRes<WriteAtomicD, [FormosaUnitMem]>;

def : WriteRes<WriteAtomicLDW, [FormosaUnitMem]>;
def : WriteRes<WriteAtomicLDD, [FormosaUnitMem]>;
}

def : WriteRes<WriteAtomicSTW, [FormosaUnitMem]>;
def : WriteRes<WriteAtomicSTD, [FormosaUnitMem]>;


// FP: every op goes to the FPU (15 + 10 cycles, fully pipelined)
let Latency = 25 in {
def : WriteRes<WriteFAdd32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFSGNJ32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMinMax32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFAdd64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFSGNJ64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMinMax64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtI32ToF32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtI32ToF64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtI64ToF32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtI64ToF64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtF32ToI32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtF32ToI64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtF64ToI32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtF64ToI64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtF32ToF64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCvtF64ToF32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFClass32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFClass64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCmp32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFCmp64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMovF32ToI32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMovI32ToF32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMovF64ToI64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMovI64ToF64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMul32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMA32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMul64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMA64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFDiv32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFDiv64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFSqrt32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFSqrt64, [FormosaUnitFPALU]>;
}

// Others
def : WriteRes<WriteCSR, [FormosaUnitIALU]> { let Latency = 13; } // CSR goes through ALU
def : WriteRes<WriteNop, []>;

def : InstRW<[WriteIALU], (instrs COPY)>;
def : InstRW<[WriteAtomicW], (instregex "^AMOCAS_W")>;
def : InstRW<[WriteAtomicD], (instregex "^AMOCAS_D_RV64")>;

// Following config is directly copy from RocketModel
//===----------------------------------------------------------------------===//
// Bypass and advance
def : ReadAdvance<ReadJmp, 0>;
def : ReadAdvance<ReadJalr, 0>;
def : ReadAdvance<ReadCSR, 0>;
def : ReadAdvance<ReadStoreData, 0>;
def : ReadAdvance<ReadMemBase, 0>;
def : ReadAdvance<ReadIALU, 0>;
def : ReadAdvance<ReadIALU32, 0>;
def : ReadAdvance<ReadShiftImm, 0>;
def : ReadAdvance<ReadShiftImm32, 0>;
def : ReadAdvance<ReadShiftReg, 0>;
def : ReadAdvance<ReadShiftReg32, 0>;
def : ReadAdvance<ReadIDiv, 0>;
def : ReadAdvance<ReadIDiv32, 0>;
def : ReadAdvance<ReadIRem, 0>;
def : ReadAdvance<ReadIRem32, 0>;
def : ReadAdvance<ReadIMul, 0>;
def : ReadAdvance<ReadIMul32, 0>;
def : ReadAdvance<ReadAtomicWA, 0>;
def : ReadAdvance<ReadAtomicWD, 0>;
def : ReadAdvance<ReadAtomicDA, 0>;
def : ReadAdvance<ReadAtomicDD, 0>;
def : ReadAdvance<ReadAtomicLDW, 0>;
def : ReadAdvance<ReadAtomicLDD, 0>;
def : ReadAdvance<ReadAtomicSTW, 0>;
def : ReadAdvance<ReadAtomicSTD, 0>;
def : ReadAdvance<ReadFStoreData, 0>;
def : ReadAdvance<ReadFMemBase, 0>;
def : ReadAdvance<ReadFAdd32, 0>;
def : ReadAdvance<ReadFAdd64, 0>;
def : ReadAdvance<ReadFMul32, 0>;
def : ReadAdvance<ReadFMul64, 0>;
def : ReadAdvance<ReadFMA32, 0>;
def : ReadAdvance<ReadFMA32Addend, 0>;
def : ReadAdvance<ReadFMA64, 0>;
def : ReadAdvance<ReadFMA64Addend, 0>;
def : ReadAdvance<ReadFDiv32, 0>;
def : ReadAdvance<ReadFDiv64, 0>;
def : ReadAdvance<ReadFSqrt32, 0>;
def : ReadAdvance<ReadFSqrt64, 0>;
def : ReadAdvance<ReadFCmp32, 0>;
def : ReadAdvance<ReadFCmp64, 0>;
def : ReadAdvance<ReadFSGNJ32, 0>;
def : ReadAdvance<ReadFSGNJ64, 0>;
def : ReadAdvance<ReadFMinMax32, 0>;
def : ReadAdvance<ReadFMinMax64, 0>;
def : ReadAdvance<ReadFCvtF32ToI32, 0>;
def : ReadAdvance<ReadFCvtF32ToI64, 0>;
def : ReadAdvance<ReadFCvtF64ToI32, 0>;
def : ReadAdvance<ReadFCvtF64ToI64, 0>;
def : ReadAdvance<ReadFCvtI32ToF32, 0>;
def : ReadAdvance<ReadFCvtI32ToF64, 0>;
def : ReadAdvance<ReadFCvtI64ToF32, 0>;
def : ReadAdvance<ReadFCvtI64ToF64, 0>;
def : ReadAdvance<ReadFCvtF32ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF32, 0>;
def : ReadAdvance<ReadFMovF32ToI32, 0>;
def : ReadAdvance<ReadFMovI32ToF32, 0>;
def : ReadAdvance<ReadFMovF64ToI64, 0>;
def : ReadAdvance<ReadFMovI64ToF64, 0>;
def : ReadAdvance<ReadFClass32, 0>;
def : ReadAdvance<ReadFClass64, 0>;

//===----------------------------------------------------------------------===//
// Unsupported extensions
defm : UnsupportedSchedV;
defm : UnsupportedSchedZabha;
defm : UnsupportedSchedZba;
defm : UnsupportedSchedZbb;
defm : UnsupportedSchedZbc;
defm : UnsupportedSchedZbs;
defm : UnsupportedSchedZbkb;
defm : UnsupportedSchedZbkx;
defm : UnsupportedSchedZfa;
defm : UnsupportedSchedZfhmin;
defm : UnsupportedSchedSFB;
defm : UnsupportedSchedZvk;
defm : UnsupportedSchedXsfvcp;
}
