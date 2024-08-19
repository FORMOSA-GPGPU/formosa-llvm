#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

using namespace llvm;
#define DEBUG_TYPE "RISCVFSAInsertMinPCPri"

namespace {
class RISCVFSAInsertMinPCPri : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;

public:
  static char ID;
  RISCVFSAInsertMinPCPri() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "RISCVFSAInsertMinPCPri"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAInsertMinPCPri::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSAInsertMinPCPri, DEBUG_TYPE,
    "FSA update branch instructions by inserting fsa.pri instructions", false,
    false)
INITIALIZE_PASS_END(
    RISCVFSAInsertMinPCPri, DEBUG_TYPE,
    "FSA update branch instructions by inserting fsa.pri instructions", false,
    false)

void RISCVFSAInsertMinPCPri::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAInsertMinPCPri::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  LLVM_DEBUG(dbgs() << "Pass RISCVFSAInsertMinPCPri: " << MF.getName() << "\n");
  initialize(MF);
  bool MadeChange = false;

  if (!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Divergence)) {
    LLVM_DEBUG(dbgs() << "Function has no divergence, skip\n");
    return false;
  }
  LLVM_DEBUG(dbgs() << "Function: <" << MF.getName()
                    << "> has divergence, perform "
                       "MinPC optimization\n");

  unsigned int NumMBBs = MF.getNumBlockIDs();
  LLVM_DEBUG(dbgs() << "Number of MBBs: " << NumMBBs << "\n");
  if (NumMBBs > 63) {
    report_fatal_error("Number of basic blocks exceeds 63, cannot insert "
                       "fsa.pri.set instructions");
  }
  for (MachineBasicBlock &MBB : MF) {
    // set the priority based on the occurrence of basic blocks, basic blocks
    // with lower PC value have higher priority
    // Insert fsa.pri.set <priority>
    unsigned BlockPriority = NumMBBs - MBB.getNumber();
    LLVM_DEBUG(dbgs() << "    BB priority: " << BlockPriority << "\n");
    BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
            TII->get(RISCV::FSA_PRI_SET))
        .addImm(BlockPriority);
    MadeChange = true;

    for (MachineInstr &MI : MBB) {
      // if MI is a call node, insert fsa.pri.set after it
      if (MI.isCall()) {
        MachineInstr &NextMI = *std::next(MI.getIterator());
        BuildMI(MBB, NextMI.getIterator(), NextMI.getDebugLoc(),
                TII->get(RISCV::FSA_PRI_SET))
            .addImm(BlockPriority);
      }
    }
  }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAInsertMinPCPriPass() {
  return new RISCVFSAInsertMinPCPri();
}
