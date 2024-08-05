#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

#include <iterator>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPDomLvBasedPriority"

namespace {
class RISCVFSAPDomLvBasedPriority : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;

public:
  static char ID;
  RISCVFSAPDomLvBasedPriority() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAPDomLvBasedPriority";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAPDomLvBasedPriority::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAPDomLvBasedPriority, DEBUG_TYPE,
                      "FSA handling PDom priority by inserting fsa.pri "
                      "instructions based on PDom level",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPDomLvBasedPriority, DEBUG_TYPE,
                    "FSA handling PDom priority by inserting fsa.pri "
                    "instructions based on PDom level",
                    false, false)

void RISCVFSAPDomLvBasedPriority::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPDomLvBasedPriority::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPDomLvBasedPriority on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.getFirstTerminator();
         I != MBB.end(); I = std::next(I)) {
      MachineInstr &MI = *I;
      if (MI.isReturn()) {
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::FSA_PRI_RESET));
      }
    }

    DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
    if (!PDomNode) {
      LLVM_DEBUG(dbgs() << "Cannot find IPDOM for current machine basic block "
                        << MBB.getName() << "\n";);
      continue;
    }

    // set the priority based on the level of IDom
    LLVM_DEBUG(dbgs() << "BB " << MBB.getName()
                      << " priority: " << PDomNode->getLevel() << "\n");
    int PDomLv = PDomNode->getLevel();
    if(PDomLv > 63){
      report_fatal_error("Number of PDom level exceeds 63, cannot insert "
                        "fsa.pri.set instructions");
    }

    BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
            TII->get(RISCV::FSA_PRI_SET))
        .addImm(PDomLv);
    MadeChange = true;
  }

  // insert fsa.pri.base at the beginning of the function
  MachineBasicBlock &MBB = MF.front();
  BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
          TII->get(RISCV::FSA_PRI_BASE));
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPDomLvBasedPriorityPass() {
  return new RISCVFSAPDomLvBasedPriority();
}