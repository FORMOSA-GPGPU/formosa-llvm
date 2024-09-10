#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

using namespace llvm;
#define DEBUG_TYPE "RISCVFSAInsertFunctPri"

namespace {
class RISCVFSAInsertFunctPri : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;

public:
  static char ID;
  RISCVFSAInsertFunctPri() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "RISCVFSAInsertFunctPri"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAInsertFunctPri::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAInsertFunctPri, DEBUG_TYPE,
                      "FSA Insert function priority adjustment instructions",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineUniformityAnalysisPass)
INITIALIZE_PASS_END(RISCVFSAInsertFunctPri, DEBUG_TYPE,
                    "FSA Insert function priority adjustment instructions",
                    false, false)

void RISCVFSAInsertFunctPri::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAInsertFunctPri::runOnMachineFunction(MachineFunction &MF) {
  initialize(MF);

  // Insert priority raise at the beginning of the function
  BuildMI(*MF.begin(), MF.begin()->begin(), DebugLoc(),
          TII->get(RISCV::FSA_PRI_RAISE_F));

  // Insert priority lower before returning from the function
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isReturn()) {
        BuildMI(MBB, MI, DebugLoc(), TII->get(RISCV::FSA_PRI_LOWER_F));
      }
    }
  }

  return true;
}

FunctionPass *llvm::createRISCVFSAInsertFunctPriPass() {
  return new RISCVFSAInsertFunctPri();
}
