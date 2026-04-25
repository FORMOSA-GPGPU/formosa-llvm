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
                      "instructions based on PDom level, use argument "
                      "-fsa-pdom-level to enable the pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPDomLevelBasedPriority, DEBUG_TYPE,
                    "FSA handling PDom priority by inserting fsa.pri "
                    "instructions based on PDom level, use argument "
                    "-fsa-pdom-level to enable the pass",
                    false, false)

void RISCVFSAPDomLevelBasedPriority::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPDomLevelBasedPriority::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPDomLevelBasedPriority on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  bool AllowSkip = (MF.getTarget().getOptLevel() != CodeGenOptLevel::None);

  if ((!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Divergence)) &&
      AllowSkip) {
    LLVM_DEBUG(dbgs() << "Function does not have divergence, skip priority "
                         "instructions insertion\n");
    return false;
  }

  for (MachineBasicBlock &MBB : MF) {
    DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);

    bool IsRedundantInsertion = MadeChange;
    if (!PDomNode) {
      LLVM_DEBUG(
          dbgs() << "Cannot find PDom node for current machine basic block "
                 << MBB.getName() << "\n";);
      continue;
    }

    unsigned int PDomLevel = PDomNode->getLevel();

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
