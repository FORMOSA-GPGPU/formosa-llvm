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
#define DEBUG_TYPE "RISCVFSAGreedyPDomLevel"

namespace {
class RISCVFSAGreedyPDomLevel : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;

public:
  static char ID;
  RISCVFSAGreedyPDomLevel() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAGreedyPDomLevel";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAGreedyPDomLevel::ID = 0;
uint64_t MaxReconvPri = 0;
uint64_t LastPri = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAGreedyPDomLevel, DEBUG_TYPE,
                      "FSA handling PDom priority by inserting fsa.pri "
                      "instructions based on PDom level, use argument "
                      "-fsa-pdom-level to enable the pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAGreedyPDomLevel, DEBUG_TYPE,
                    "FSA handling PDom priority by inserting fsa.pri "
                    "instructions based on PDom level, use argument "
                    "-fsa-pdom-level to enable the pass",
                    false, false)

void RISCVFSAGreedyPDomLevel::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAGreedyPDomLevel::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAGreedyPDomLevel on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  // Skip insertion only when opt level is not none
  bool AllowSkip = (MF.getTarget().getOptLevel() != CodeGenOptLevel::None);

  if ((!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Divergence)) &&
      AllowSkip) {
    LLVM_DEBUG(dbgs() << "Function does not have divergence, skip priority "
                         "instructions insertion\n");
    return false;
  }


  unsigned PDomLevel = 0;
  for (MachineBasicBlock &MBB : MF) {
    DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
    if (!PDomNode) {
      LLVM_DEBUG(
          dbgs() << "Cannot find PDom node for current machine basic block "
                 << MBB.getName() << "\n";);
        PDomLevel = LastPri;
    //   continue;
    } else {
        PDomLevel = PDomNode->getLevel();
        LastPri = PDomLevel;
    }
    if(MBB.pred_size() > 1) {
        if(PDomLevel > MaxReconvPri) {
            MaxReconvPri = PDomLevel;
        }
    }
  }

  LastPri = 0;
  PDomLevel = 0;
  for (MachineBasicBlock &MBB : MF) {
    DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
    if (!PDomNode) {
      LLVM_DEBUG(
          dbgs() << "Cannot find PDom node for current machine basic block "
                 << MBB.getName() << "\n";);
        PDomLevel = LastPri;
    } else {
        PDomLevel = PDomNode->getLevel();
        LastPri = PDomLevel;
    }

    // set the priority based on the level of IDom
    LLVM_DEBUG(dbgs() << "BB " << MBB.getName() << " priority: " << PDomLevel
                      << "\n");
    if (PDomLevel > 63) {
      report_fatal_error("Number of PDom level exceeds 63, cannot insert "
                         "fsa.pri.set instructions");
    }
    uint64_t FinalLevel = PDomLevel;

    if(MBB.pred_size() == 1) {
        FinalLevel += MaxReconvPri;
    }
    BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
            TII->get(RISCV::FSA_PRI_SET))
        .addImm(FinalLevel);
    MadeChange = true;
  }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAGreedyPDomLevelPass() {
  return new RISCVFSAGreedyPDomLevel();
}