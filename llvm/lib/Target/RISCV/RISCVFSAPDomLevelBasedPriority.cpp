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
#define DEBUG_TYPE "RISCVFSAPDomLevelBasedPriority"

namespace {
class RISCVFSAPDomLevelBasedPriority : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;

public:
  static char ID;
  RISCVFSAPDomLevelBasedPriority() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAPDomLevelBasedPriority";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAPDomLevelBasedPriority::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAPDomLevelBasedPriority, DEBUG_TYPE,
                      "FSA handling PDom priority by inserting fsa.pri "
                      "instructions based on PDom level",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPDomLevelBasedPriority, DEBUG_TYPE,
                    "FSA handling PDom priority by inserting fsa.pri "
                    "instructions based on PDom level",
                    false, false)

void RISCVFSAPDomLevelBasedPriority::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPDomLevelBasedPriority::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPDomLevelBasedPriority on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  // Skip insertion only when opt level is not none
  bool allowSkip = (MF.getTarget().getOptLevel() != CodeGenOptLevel::None);

  if ((!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Divergence)) && allowSkip) {
    LLVM_DEBUG(dbgs() << "Function does not have divergence, skip priority "
                         "instructions insertion\n");
    return false;
  }

  for (MachineBasicBlock &MBB : MF) {
    DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);

    // Skip insertion of the `pri.set` instruction ONLY if it has already been
    // inserted previously (indicated by MadeChange being true). This is
    // especially for the entry block, which has no predecessors and therefore
    // does not need a redundant check. To ensure that a `pri.set` instruction
    // is inserted in the entry block, we initialize "IsRedundantInsertion" to
    // false (the value of MadeChange) at the beginning of processing for each
    // block.
    bool IsRedundantInsertion = MadeChange;
    if (!PDomNode) {
      LLVM_DEBUG(
          dbgs() << "Cannot find PDom node for current machine basic block "
                 << MBB.getName() << "\n";);
      continue;
    }

    unsigned int PDomLevel = PDomNode->getLevel();

    // "CPNLevel" stands for "corresponding PDom node level in the PDom tree."
    // If the CPNLevel of the current MBB is the same as that of all its
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
      if (PredPDomNode->getLevel() != PDomLevel) {
        IsRedundantInsertion = false;
        break;
      }
    }

    if (IsRedundantInsertion) {
      LLVM_DEBUG(dbgs() << "BB " << MBB.getName()
                        << "has the same priority of all it's predecessors: "
                        << "with level" << PDomLevel << ", skip insertion\n");
      continue;
    }

    // set the priority based on the level of IDom
    LLVM_DEBUG(dbgs() << "BB " << MBB.getName() << " priority: " << PDomLevel
                      << "\n");
    if (PDomLevel > 63) {
      report_fatal_error("Number of PDom level exceeds 63, cannot insert "
                         "fsa.pri.set instructions");
    }

    BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
            TII->get(RISCV::FSA_PRI_SET))
        .addImm(PDomLevel);
    MadeChange = true;
  }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPDomLevelBasedPriorityPass() {
  return new RISCVFSAPDomLevelBasedPriority();
}