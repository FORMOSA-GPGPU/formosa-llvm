// Lunar gpgpu banked register layout

def LunarModel : SchedMachineModel {
    // CompleteModel = 1 means the model in this file is completely well-defined and ask llvm
    // Do not use any other possible model assumption when scheduling
    let CompleteModel = 0;  // Whether all instructions is well-defined in this model

    let IssueWidth = 1;

    // Need to check following:
    // MicroOpBufferSize = 1 means that instructions will always be added
    // the ready queue when they become available.  This exposes them
    // to the register pressure analysis.

    let FullInstRWOverlapCheck = 1;
    let EnableIntervals = 1;
    let MicroOpBufferSize = 0; // InOrder pipeline
    let LoopMicroOpBufferSize = 32; // profiled value
    let LoadLatency = 8; // Refer to HitLantency

    // profile if this is needed
    let PostRAScheduler = 1;

    // TODO: profile for best penalty value
    int MispredictPenalty = 10;

    let UnsupportedFeatures = [HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                                HasStdExtZcmt, HasStdExtZknd, HasStdExtZkne,
                                HasStdExtZknh, HasStdExtZksed, HasStdExtZksh,
                                HasStdExtZkr, HasVInstructions, HasVInstructionsI64];
}

// BufferSize : refer to kInstrQueue
let BufferSize = 2 in {
    def LunarUnitIALU       : ProcResource<1>; // Int ALU(w/o Mult and Div)
    def LunarUnitIMul       : ProcResource<1>; // Int Mult Unit
    def LunarUnitIDIV       : ProcResource<1>; // Int Div Unit
    def LunarMisc           : ProcResource<1>;
    def LunarUnitB          : ProcResource<1>; // Branch, currently unused
}

// possible more BufferSize ?
let BufferSize = 8 in {
    def LunarUnitLD        : ProcResource<1>; // Load
}

let BufferSize = 4 in {
    def LunarUnitST        : ProcResource<1>; // Store
}

let BufferSize = 0 in {
def LunarAtomic            : ProcResource<1>;
}

//===----------------------------------------------------------------------===//

let SchedModel = LunarModel in {

def : WriteRes<WriteJmp, [LunarUnitIALU]>;
def : WriteRes<WriteJal, [LunarUnitIALU]>;
def : WriteRes<WriteJalr, [LunarUnitIALU]>;

// Integer arithmetic and logic
let Latency = 1 in {
def : WriteRes<WriteIALU32, [LunarUnitIALU]>;
def : WriteRes<WriteIALU, [LunarUnitIALU]>;
def : WriteRes<WriteShiftImm32, [LunarUnitIALU]>;
def : WriteRes<WriteShiftImm, [LunarUnitIALU]>;
def : WriteRes<WriteShiftReg32, [LunarUnitIALU]>;
def : WriteRes<WriteShiftReg, [LunarUnitIALU]>;
}

// Integer multiplication
let Latency = 4 in {
def : WriteRes<WriteIMul, [LunarUnitIMul]>;
def : WriteRes<WriteIMul32, [LunarUnitIMul]>;
}

// Integer DIV/REM
let Latency = 16 in {
def : WriteRes<WriteIDiv32, [LunarUnitIDIV]>;
def : WriteRes<WriteIDiv, [LunarUnitIDIV]>;
def : WriteRes<WriteIRem32, [LunarUnitIDIV]>;
def : WriteRes<WriteIRem, [LunarUnitIDIV]>;
}

// Memory
let Latency = 6 in {
def : WriteRes<WriteSTB, [LunarUnitST]>;
def : WriteRes<WriteSTH, [LunarUnitST]>;
def : WriteRes<WriteSTW, [LunarUnitST]>;
def : WriteRes<WriteSTD, [LunarUnitST]>;
}

let Latency = 8 in {
def : WriteRes<WriteLDB, [LunarUnitLD]>;
def : WriteRes<WriteLDH, [LunarUnitLD]>;
}

let Latency = 8 in {
def : WriteRes<WriteLDW, [LunarUnitLD]>;
def : WriteRes<WriteLDD, [LunarUnitLD]>;
}

// Atomic operation
let Latency = 8 in {
def : WriteRes<WriteAtomicLDW, [LunarAtomic]>;
def : WriteRes<WriteAtomicLDD, [LunarAtomic]>;
def : WriteRes<WriteAtomicSTW, [LunarAtomic]>;
def : WriteRes<WriteAtomicSTD, [LunarAtomic]>;
def : WriteRes<WriteAtomicW, [LunarAtomic]>;
def : WriteRes<WriteAtomicD, [LunarAtomic]>;
}

// Others
def : WriteRes<WriteCSR, [LunarMisc]>;
def : WriteRes<WriteNop, []>;

def : InstRW<[WriteIALU], (instrs COPY)>;

// Following config is directly copy from RocketModel
//===----------------------------------------------------------------------===//
// Bypass and advance
def : ReadAdvance<ReadJmp, 3>;
def : ReadAdvance<ReadJalr, 3>;
def : ReadAdvance<ReadCSR, 0>;

def : ReadAdvance<ReadIALU,       0, [WriteLDB, WriteLDH, WriteLDW, WriteLDD]>;
def : ReadAdvance<ReadIALU32,     0, [WriteLDB, WriteLDH, WriteLDW, WriteLDD]>;
def : ReadAdvance<ReadShiftImm,   0, [WriteLDB, WriteLDH, WriteLDW, WriteLDD]>;
def : ReadAdvance<ReadShiftImm32, 0, [WriteLDB, WriteLDH, WriteLDW, WriteLDD]>;
def : ReadAdvance<ReadShiftReg,   0, [WriteLDB, WriteLDH, WriteLDW, WriteLDD]>;
def : ReadAdvance<ReadShiftReg32, 0, [WriteLDB, WriteLDH, WriteLDW, WriteLDD]>;

def : ReadAdvance<ReadStoreData, 0>;
def : ReadAdvance<ReadMemBase  , 0>;
def : ReadAdvance<ReadIDiv     , 0>;
def : ReadAdvance<ReadIDiv32   , 0>;
def : ReadAdvance<ReadIRem     , 0>;
def : ReadAdvance<ReadIRem32   , 0>;
def : ReadAdvance<ReadIMul     , 0>;
def : ReadAdvance<ReadIMul32   , 0>;

def : ReadAdvance<ReadAtomicWA, 0>;
def : ReadAdvance<ReadAtomicWD, 0>;
def : ReadAdvance<ReadAtomicDA, 0>;
def : ReadAdvance<ReadAtomicDD, 0>;
def : ReadAdvance<ReadAtomicLDW, 0>;
def : ReadAdvance<ReadAtomicLDD, 0>;
def : ReadAdvance<ReadAtomicSTW, 0>;
def : ReadAdvance<ReadAtomicSTD, 0>;

//===----------------------------------------------------------------------===//
// Unsupported extensions
// defm : UnsupportedSchedD;    // This is incluede in UnsupportedSchedF
// defm : UnsupportedSchedZfh;  // This is incluede in UnsupportedSchedF
defm : UnsupportedSchedF;
defm : UnsupportedSchedV;
defm : UnsupportedSchedZabha;
defm : UnsupportedSchedZba;
defm : UnsupportedSchedZbb;
defm : UnsupportedSchedZbc;
defm : UnsupportedSchedZbs;
defm : UnsupportedSchedZbkb;
defm : UnsupportedSchedZbkx;
defm : UnsupportedSchedZfa;
defm : UnsupportedSchedSFB;
defm : UnsupportedSchedZvk;
defm : UnsupportedSchedXsfvcp;
}
