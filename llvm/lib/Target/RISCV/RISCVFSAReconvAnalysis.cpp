//===- RISCVFSAReconvAnalysis.cpp -------------------------------*- C++ -*-===//
//
// Pre-RA analysis to propagate divergence from thread-id / local-id / etc.
// and mark reconvergence basic blocks in RISCVFsaMFInfo.
//
//===----------------------------------------------------------------------===//

#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "RISCVMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineUniformityAnalysis.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineSSAContext.h"
#include "llvm/ADT/GenericUniformityImpl.h"
#include <unordered_set>

using namespace llvm;
#define DEBUG_TYPE "RISCVFSAReconvAnalysis"

namespace {

class RISCVFSAReconvAnalysis : public MachineFunctionPass {
private:
  const RISCVRegisterInfo *TRI = nullptr;
  const RISCVInstrInfo *TII = nullptr;
  MachineUniformityInfo *MUI = nullptr;
  RISCVMachineFunctionInfo* MFI = nullptr;
  MachinePostDominatorTree *PDT = nullptr;
  MachineDominatorTree *DT = nullptr;
  MachineCycleInfo *CI = nullptr;

public:
  static char ID;
  RISCVFSAReconvAnalysis() : MachineFunctionPass(ID) {}

  void initialize(MachineFunction &MF);

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "RISCVFSAReconvAnalysis"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineUniformityAnalysisPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineCycleInfoWrapperPass>();

    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachinePostDominatorTreeWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineCycleInfoWrapperPass>();
    AU.addPreserved<MachineUniformityAnalysisPass>();
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  SmallVector<MachineBasicBlock*, 8> possibleReconvBBs(MachineBasicBlock *X);
};

} // end anonymous namespace

char RISCVFSAReconvAnalysis::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAReconvAnalysis, DEBUG_TYPE,
                      "FSA reconvergence analysis", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineUniformityAnalysisPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineCycleInfoWrapperPass)
INITIALIZE_PASS_END(RISCVFSAReconvAnalysis, DEBUG_TYPE,
                    "FSA reconvergence analysis", false, false)

SmallVector<MachineBasicBlock*, 8> RISCVFSAReconvAnalysis::possibleReconvBBs(MachineBasicBlock *X) {
  SmallVector<MachineBasicBlock*, 8> Out;
  unsigned NSucc = X->succ_size();
  assert(NSucc < 32 && "Too much succ to be handled!\n");
  if (NSucc <= 1)
    return Out;
  // IPDOM must be a reconv bb if exist
  if (auto *Node = PDT->getNode(X)) {
    if (auto *IDomNode = Node->getIDom()) {
      if (auto *BB = IDomNode->getBlock()) {
        Out.push_back(BB);
      }
    }
  }
  DenseMap<MachineBasicBlock*, unsigned> ReachMask;

  unsigned Bit = 1;
  SmallVector<MachineBasicBlock*, 64> Worklist;

  for (MachineBasicBlock *Start : X->successors()) {
    if(Start == X) {
      Out.push_back(Start);
      continue;
    }
    unsigned &Mask = ReachMask[Start];
    if ((Mask & Bit) == 0) {
      Mask |= Bit;
      Worklist.push_back(Start);
    }
    Bit <<= 1;
  }

  while(!Worklist.empty()) {
    MachineBasicBlock *MBB = Worklist.pop_back_val();
    if(MBB == X)
      continue;
    // MBB Pdom X, no need to propogate
    if(PDT->dominates(MBB, X))
      continue;

    unsigned &Mask = ReachMask[MBB];

    for (MachineBasicBlock *Succ : MBB->successors()) {
      if(Succ == MBB)
        continue;

      unsigned &SMask = ReachMask[Succ];
      unsigned NewMask = SMask | Mask;

      if (NewMask != SMask) {
        SMask = NewMask;
        Worklist.push_back(Succ);
      }
    }
  }

  for (auto &[BB, Mask] : ReachMask) {
    // only one succ reach here, skip
    if ((Mask & (Mask - 1)) == 0) {
      continue;
    }

    if (!DT->dominates(X, BB))
      continue;

    if (BB->pred_size() < 2)
      continue;

    // more than one successor can reach here
    Out.push_back(BB);
  }

  return Out;
}


void RISCVFSAReconvAnalysis::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  DT  = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  PDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  MUI = &getAnalysis<MachineUniformityAnalysisPass>().getUniformityInfo();
  CI  = &getAnalysis<MachineCycleInfoWrapperPass>().getCycleInfo();
  MFI = MF.getInfo<RISCVMachineFunctionInfo>();
  MFI->resetRBBSet();
}

bool RISCVFSAReconvAnalysis::runOnMachineFunction(MachineFunction &MF) {
  bool changed = false;


  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  initialize(MF);
  SmallVector<MachineBasicBlock*, 8> DivergentBranchMBBs;
  MachineSSAContext SSAContext(&MF);
  GenericSyncDependenceAnalysis<MachineSSAContext> SDA(SSAContext, *DT, *CI);
  std::unordered_set<MachineBasicBlock *> ReconvBBSet;

  for(auto &MBB : MF) {
    if(MUI->hasDivergentTerminator(MBB)) {
      const auto &DivDesc = SDA.getJoinBlocks(&MBB);
      for (const MachineBasicBlock *JoinBB : DivDesc.JoinDivBlocks) {
        MachineBasicBlock *ReconvBB = const_cast<MachineBasicBlock*>(JoinBB);
        ReconvBBSet.insert(ReconvBB);
        // llvm::dbgs() << "MBB" << ReconvBB->getNumber() << " is mark reconv\n";
        // auto InsertPt = ReconvBB->SkipPHIsLabelsAndDebug(ReconvBB->begin());
        // BuildMI(*ReconvBB, InsertPt, ReconvBB->findDebugLoc(InsertPt), TII->get(RISCV::FSA_RECONV_MARKER));
        // changed = true;
      }
      // // DivergentBranchMBBs.push_back(&MBB);
      for (const MachineBasicBlock *CycleExit : DivDesc.CycleDivBlocks) {
        MachineBasicBlock *ReconvBB = const_cast<MachineBasicBlock*>(CycleExit);
        ReconvBBSet.insert(ReconvBB);
        // llvm::dbgs() << "MBB" << ReconvBB->getNumber() << " is mark reconv\n";
        // auto InsertPt = ReconvBB->SkipPHIsLabelsAndDebug(ReconvBB->begin());
        // BuildMI(*ReconvBB, InsertPt, ReconvBB->findDebugLoc(InsertPt), TII->get(RISCV::FSA_RECONV_MARKER));
        // changed = true;
      }
    }
  }

  for (auto *ReconvBB : ReconvBBSet) {
    llvm::dbgs() << "MBB" << ReconvBB->getNumber() << " is mark reconv\n";
    auto InsertPt = ReconvBB->SkipPHIsLabelsAndDebug(ReconvBB->begin());
    BuildMI(*ReconvBB, InsertPt, ReconvBB->findDebugLoc(InsertPt), TII->get(RISCV::FSA_RECONV_MARKER));
  }
  // for(auto &MBB : DivergentBranchMBBs) {
  //   for (auto *ReconvBB : possibleReconvBBs(MBB)) {
  //     auto InsertPt = ReconvBB->SkipPHIsLabelsAndDebug(ReconvBB->begin());
  //     BuildMI(*ReconvBB, InsertPt, ReconvBB->findDebugLoc(InsertPt),
  //     TII->get(RISCV::FSA_RECONV_MARKER));
  //     // MFI->markReconv(ReconvBB);
  //     changed = true;
  //   }
  // }
  return changed;
}

// Factory function
FunctionPass *llvm::createRISCVFSAReconvAnalysisPass() {
  return new RISCVFSAReconvAnalysis();
}
