#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include <unordered_set>
#include <iterator>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAIPDOMLike"

namespace {
class RISCVFSAIPDOMLike : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MBBPostDominatorTree;
  MachineLoopInfo *MLI;

public:
  static char ID;
  RISCVFSAIPDOMLike() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "RISCVFSAIPDOMLike"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAIPDOMLike::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSAIPDOMLike, DEBUG_TYPE,
    "FSA divergence handling by inserting fsa.pri instructions, "
    "use argument -fsa-ipdom-like to enable the pass",
    false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(
    RISCVFSAIPDOMLike, DEBUG_TYPE,
    "FSA divergence handling by inserting fsa.pri instructions, "
    "use argument -fsa-ipdom-like to enable the pass",
    false, false)

void RISCVFSAIPDOMLike::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MBBPostDominatorTree = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAIPDOMLike::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAIPDOMLike on function: " << MF.getName()
             << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  std::unordered_set<MachineBasicBlock *> ImmPostDominatorMBBSet;

  for (MachineBasicBlock &MBB : MF) {
    if(MBB.succ_empty()) {
      ExitMBBSet.insert(&MBB);
    }
    bool MBBIsDiverged = MBB.succ_size() > 1;
    if (MBBIsDiverged) {
      DomTreeNodeBase<MachineBasicBlock> *PDomNode = MBBPostDominatorTree->getNode(&MBB);
      if(PDomNode->getIDom()) {
        ImmPostDominatorMBBSet.insert(PDomNode->getIDom()->getBlock());
      }
    }
  }

  for (MachineBasicBlock *MBB : ImmPostDominatorMBBSet) {
    if(ExitMBBSet.count(MBB))
      continue; // Pri of exit point will be dealed with later, skip

    // Insert Lower at begining to wait other threads for reconverge
    BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
    TII->get(RISCV::FSA_PRI_LOWER));

    auto MBBTermIt = MBB->getFirstTerminator();
    // Insert Raise at end to resume higher priority
    BuildMI(*MBB, MBBTermIt, MBB->findDebugLoc(MBBTermIt),
      TII->get(RISCV::FSA_PRI_RAISE));
    MadeChange = true;
  }
  
  if(MadeChange) {
    MachineBasicBlock &EntryMBB = *MF.begin();
    auto EntryTermIt = EntryMBB.getFirstTerminator();
    // At begining, raise every thread's pri
    BuildMI(EntryMBB, EntryTermIt, EntryMBB.findDebugLoc(EntryTermIt),
      TII->get(RISCV::FSA_PRI_RAISE));

    // At every exit, lower pri to reset priority
    for(MachineBasicBlock *MBB : ExitMBBSet) {
      BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
      TII->get(RISCV::FSA_PRI_LOWER));
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAIPDOMLikePass() {
  return new RISCVFSAIPDOMLike();
}
