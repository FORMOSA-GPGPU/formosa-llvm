// Formosa gpgpu banked register layout

def FormosaModel : SchedMachineModel {
    // CompleteModel = 1 means the model in this file is completely well-defined and ask llvm
    // Do not use any other possible model assumption when scheduling
    let CompleteModel = 0;  // Whether all instructions is well-defined in this model

    let IssueWidth = 2;
    
    // Need to check following:
    // MicroOpBufferSize = 1 means that instructions will always be added
    // the ready queue when they become available.  This exposes them
    // to the register pressure analysis.

    
    let MicroOpBufferSize = 2; // Max micro-ops that can be buffered, 
                               // should be determine by min(reservation stations, #of physical reg for renaming, ROB size)
    
    let LoadLatency = 4; // Refer to HitLantency

    // profile if this is needed
    let PostRAScheduler = 1;

    // TODO: profile for best penalty value
    int MispredictPenalty = 2;

    let UnsupportedFeatures = [HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                                HasStdExtZcmt, HasStdExtZknd, HasStdExtZkne,
                                HasStdExtZknh, HasStdExtZksed, HasStdExtZksh,
                                HasStdExtZkr, HasVInstructions, HasVInstructionsI64];
}

// BufferSize : refer to kInstrQueue
let BufferSize = 2 in {
    def FormosaUnitIALU       : ProcResource<1>; // Int ALU(w/o Mult and Div)
    def FormosaUnitIMul       : ProcResource<1>; // Int Mult Unit
    def FormosaUnitIDIV       : ProcResource<1>; // Int Div Unit
    def FormosaUnitFPALU      : ProcResource<1>; // FP ALU, currently not modeling in simtix
    def FormosaUnitFPDivSqrt  : ProcResource<1>; // FP DivSqrt Unit, currently not modeling in simtix
    def FormosaMisc           : ProcResource<1>;
    // def FormosaUnitB          : ProcResource<1>; // Branch, currently unused
}

// possible more BufferSize ?
let BufferSize = 8 in {
    def FormosaUnitMem        : ProcResource<1>; // Load/Store
}

//===----------------------------------------------------------------------===//

let SchedModel = FormosaModel in {

// Branching is dispatched to IALU
let Latency = 1 in {
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

// Integer multiplication
let Latency = 2 in {
def : WriteRes<WriteIMul, [FormosaUnitIMul]>;
def : WriteRes<WriteIMul32, [FormosaUnitIMul]>;
}

// Integer DIV/REM
let Latency = 8 in {
def : WriteRes<WriteIDiv32, [FormosaUnitIDIV]>;
def : WriteRes<WriteIDiv, [FormosaUnitIDIV]>;
def : WriteRes<WriteIRem32, [FormosaUnitIDIV]>;
def : WriteRes<WriteIRem, [FormosaUnitIDIV]>;
}

// Memory
let Latency = 1 in {
def : WriteRes<WriteSTB, [FormosaUnitMem]>;
def : WriteRes<WriteSTH, [FormosaUnitMem]>;
def : WriteRes<WriteSTW, [FormosaUnitMem]>;
def : WriteRes<WriteSTD, [FormosaUnitMem]>;
def : WriteRes<WriteFST32, [FormosaUnitMem]>;
def : WriteRes<WriteFST64, [FormosaUnitMem]>;
}

let Latency = 4 in {
def : WriteRes<WriteLDB, [FormosaUnitMem]>;
def : WriteRes<WriteLDH, [FormosaUnitMem]>;
}

let Latency = 4 in {
def : WriteRes<WriteLDW, [FormosaUnitMem]>;
def : WriteRes<WriteLDD, [FormosaUnitMem]>;
def : WriteRes<WriteFLD32, [FormosaUnitMem]>;
def : WriteRes<WriteFLD64, [FormosaUnitMem]>;

// Atomic memory
def : WriteRes<WriteAtomicW, [FormosaUnitMem]>;
def : WriteRes<WriteAtomicD, [FormosaUnitMem]>;

def : WriteRes<WriteAtomicLDW, [FormosaUnitMem]>;
def : WriteRes<WriteAtomicLDD, [FormosaUnitMem]>;
}

def : WriteRes<WriteAtomicSTW, [FormosaUnitMem]>;
def : WriteRes<WriteAtomicSTD, [FormosaUnitMem]>;


// Currently We don't model fp latency in simtix!!!
// Following RocketModel's config
// Single precision.
let Latency = 4 in {
def : WriteRes<WriteFAdd32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFSGNJ32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMinMax32, [FormosaUnitFPALU]>;
}

// Double precision
let Latency = 6 in {
def : WriteRes<WriteFAdd64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFSGNJ64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMinMax64, [FormosaUnitFPALU]>;
}

// Conversions
let Latency = 2 in {
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
}

// FP multiplication
let Latency = 5 in {
def : WriteRes<WriteFMul32, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMA32, [FormosaUnitFPALU]>;
}

let Latency = 7 in {
def : WriteRes<WriteFMul64, [FormosaUnitFPALU]>;
def : WriteRes<WriteFMA64, [FormosaUnitFPALU]>;
}

// FP division
// FP division unit on Rocket is not pipelined, so set resource cycles to latency.
let Latency = 20, ReleaseAtCycles = [20] in {
def : WriteRes<WriteFDiv32, [FormosaUnitFPDivSqrt]>;
def : WriteRes<WriteFDiv64, [FormosaUnitFPDivSqrt]>;
}

// FP square root unit on Rocket is not pipelined, so set resource cycles to latency.
def : WriteRes<WriteFSqrt32, [FormosaUnitFPDivSqrt]> { let Latency = 20;
                                                      let ReleaseAtCycles = [20]; }
def : WriteRes<WriteFSqrt64, [FormosaUnitFPDivSqrt]> { let Latency = 25;
                                                      let ReleaseAtCycles = [25]; }

// Others
def : WriteRes<WriteCSR, [FormosaMisc]>;
def : WriteRes<WriteNop, []>;

def : InstRW<[WriteIALU], (instrs COPY)>;

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
defm : UnsupportedSchedZfh;
defm : UnsupportedSchedSFB;
defm : UnsupportedSchedXsfvcp;
defm : UnsupportedSchedZvk;
}
