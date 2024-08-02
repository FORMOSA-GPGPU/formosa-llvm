#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"

using namespace llvm;
#define DEBUG_TYPE "RISCVFSAInsertPri"

namespace {
class RISCVFSAInsertPri : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;

public:
  static char ID;
  RISCVFSAInsertPri() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "RISCVFSAInsertPri"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAInsertPri::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSAInsertPri, DEBUG_TYPE,
    "FSA update branch instructions by inserting fsa.pri instructions", false,
    false)
INITIALIZE_PASS_END(
    RISCVFSAInsertPri, DEBUG_TYPE,
    "FSA update branch instructions by inserting fsa.pri instructions", false,
    false)

void RISCVFSAInsertPri::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAInsertPri::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  LLVM_DEBUG(dbgs() << "Pass RISCVFSAInsertPri: " << MF.getName() << "\n");
  initialize(MF);
  bool MadeChange = false;
  unsigned int NumMBBs = MF.getNumBlockIDs();
  LLVM_DEBUG(dbgs() << "Number of MBBs: " << NumMBBs << "\n");
  if (NumMBBs > 63) {
    report_fatal_error("Number of basic blocks exceeds 63, cannot insert "
                       "fsa.pri.set instructions");
  }
  bool HasPriBase = false;
  for (MachineBasicBlock &MBB : MF) {
    // set the priority based on the occurrence of basic blocks, basic blocks
    // with lower PC value have higher priority
    // Insert fsa.pri.set <priority>
    LLVM_DEBUG(dbgs() << "    BB priority: " << NumMBBs - MBB.getNumber()
                      << "\n");
    BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
            TII->get(RISCV::FSA_PRI_SET))
        .addImm(NumMBBs - MBB.getNumber());
    MadeChange = true;
    
    // insert fsa.pri.base at the beginning of the function
    if (!HasPriBase) {
      BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
              TII->get(RISCV::FSA_PRI_BASE));
      HasPriBase = true;
    }
  }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAInsertPriPass() {
  return new RISCVFSAInsertPri();
}
