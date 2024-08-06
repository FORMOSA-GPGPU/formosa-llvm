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
    bool IsRedundantInsertion = true;
    if (!PDomNode) {
      LLVM_DEBUG(
          dbgs() << "Cannot find PDom node for current machine basic block "
                 << MBB.getName() << "\n";);
      continue;
    }

    unsigned int PDomLv = PDomNode->getLevel();

    // "CPNLv" stands for "corresponding PDom node level in the PDom tree."
    // If the CPNLv of the current MBB is the same as that of all its
    // predecessors, the insertion of the current MBB is redundant and
    // should be skipped. The following code performs this check.
    for (MachineBasicBlock *Pred : predecessors(&MBB)) {
      DomTreeNodeBase<MachineBasicBlock> *PredPDomNode = MPDT->getNode(Pred);
      if (!PredPDomNode) {
        LLVM_DEBUG(
            dbgs() << "Cannot decide all pred's value for current machine "
                      "basic block "
                   << MBB.getName()
                   << ", abort redundant insertion check for current MBB\n";);
        IsRedundantInsertion = false;
        break;
      }
      if (PredPDomNode->getLevel() != PDomLv) {
        IsRedundantInsertion = false;
        break;
      }
    }

    if (IsRedundantInsertion) {
      LLVM_DEBUG(dbgs() << "BB " << MBB.getName()
                        << "has the same priority of all it's predecessors: "
                        << "with level" << PDomLv << ", skip insertion\n");
      continue;
    }

    // set the priority based on the level of IDom
    LLVM_DEBUG(dbgs() << "BB " << MBB.getName() << " priority: " << PDomLv
                      << "\n");
    if (PDomLv > 63) {
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