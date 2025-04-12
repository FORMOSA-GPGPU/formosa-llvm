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
#include <functional>
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
  MachinePostDominatorTree *PostDominatorTree;
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
  PostDominatorTree = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
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
  // An ordered set with ascending order
  std::set<int, std::greater<int>> ImmPDomLevelSet;
  for (MachineBasicBlock &MBB : MF) {
    bool MBBIsExitBB = MBB.succ_empty();
    bool MBBIsDiverged = MBB.succ_size() > 1;
    if(MBBIsExitBB) {
      ExitMBBSet.insert(&MBB);
    }

    if (MBBIsDiverged) {
      MadeChange = true;
      DomTreeNodeBase<MachineBasicBlock> *PDomNodeOfMBB = PostDominatorTree->getNode(&MBB);
      if(PDomNodeOfMBB && PDomNodeOfMBB->getIDom()) {
        // immediate post dominator of MBB
        MachineBasicBlock *ImmPostDominatorMBB = PDomNodeOfMBB->getIDom()->getBlock();
        // Skip if immediate post dominator is self or immediate post dominator is exit bb
        // exit bb will be dealed later independently
        if(ImmPostDominatorMBB == &MBB || ImmPostDominatorMBB->succ_empty())
          continue;
        DomTreeNodeBase<MachineBasicBlock> *ImmPDomNode = PostDominatorTree->getNode(ImmPostDominatorMBB);
        ImmPostDominatorMBBSet.insert(ImmPostDominatorMBB);
        ImmPDomLevelSet.insert(ImmPDomNode->getLevel());
      }
    }
  }
  int MaxPriPairCnt = 1;
  for (MachineBasicBlock *MBB : ImmPostDominatorMBBSet) {
    DomTreeNodeBase<MachineBasicBlock> *PDomNodeOfMBB = PostDominatorTree->getNode(MBB);
    int Level = PDomNodeOfMBB->getLevel();
    auto PDomLv = ImmPDomLevelSet.find(Level);
    int PriPairCnt = std::distance(ImmPDomLevelSet.begin(), PDomLv) + 1;

    for(int i = 0; i < PriPairCnt; ++i) {
      // Insert Lower at begining to wait other threads for reconverge
      BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
      TII->get(RISCV::FSA_PRI_LOWER));
      
      auto MBBTermIt = MBB->getFirstTerminator();
      // Insert Raise at end to resume higher priority
      BuildMI(*MBB, MBBTermIt, MBB->findDebugLoc(MBBTermIt),
      TII->get(RISCV::FSA_PRI_RAISE));
    }
    MaxPriPairCnt = std::max(MaxPriPairCnt, PriPairCnt);
  }

  if(MadeChange) {
    MachineBasicBlock &EntryMBB = *MF.begin();
    auto EntryTermIt = EntryMBB.getFirstTerminator();
    for(int i = 0; i < MaxPriPairCnt; ++i) {
      // At begining, raise every thread's pri
      BuildMI(EntryMBB, EntryTermIt, EntryMBB.findDebugLoc(EntryTermIt),
      TII->get(RISCV::FSA_PRI_RAISE));
    }

    for(int i = 0; i < MaxPriPairCnt; ++i) {
      // At every exit, lower pri to reset priority
      for(MachineBasicBlock *MBB : ExitMBBSet) {
        BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
        TII->get(RISCV::FSA_PRI_LOWER));
      }
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAIPDOMLikePass() {
  return new RISCVFSAIPDOMLike();
}
