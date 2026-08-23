// llvm/lib/Transforms/Reconv/MarkReconv.cpp
// #include "llvm/Passes/PassPlugin.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Reconv/MarkReconvBB.h"

namespace llvm {
  static constexpr const char *MD_RECONV = "reconv.candidate";

// Given diverge BB X, return all possible reconvergence point
static SmallVector<BasicBlock*, 8> divergeReconveBB(BasicBlock *X, DominatorTree &DT, PostDominatorTree &PDT) {
  SmallVector<BasicBlock*, 8> Out;
  auto *TI = X->getTerminator();
  unsigned NSucc = TI->getNumSuccessors();

  if (NSucc <= 1)
    return Out;

  BasicBlock *IPDom = nullptr;
  if (auto *Node = PDT.getNode(X)) {
    if (auto *IDomNode = Node->getIDom()) {
      if((IPDom = IDomNode->getBlock())) {
        Out.push_back(IPDom);
      }
    }
  }

  using MaskT = unsigned;
  if (NSucc > std::numeric_limits<MaskT>::digits) {
    return Out;
  }

  DenseMap<BasicBlock*, MaskT> ReachMask;
  SmallVector<BasicBlock*, 16> Worklist;

  for (unsigned i = 0; i < NSucc; ++i) {
    BasicBlock *Start = TI->getSuccessor(i);

    if (Start == X)
      continue;

    MaskT Bit = MaskT(1u) << i;

    MaskT &M = ReachMask[Start];
    if ((M & Bit) == 0) {
      M |= Bit;
      Worklist.push_back(Start);
    }
  }

  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    MaskT Mask = ReachMask[BB];

    if (PDT.dominates(BB, X))
      continue;

    for (BasicBlock *Succ : successors(BB)) {
      if (Succ == X)
        continue;

      MaskT &SM = ReachMask[Succ];
      MaskT NewMask = SM | Mask;

      if (NewMask != SM) {
        SM = NewMask;
        Worklist.push_back(Succ);
      }
    }
  }

  for (auto &KV : ReachMask) {
    BasicBlock *BB = KV.first;
    MaskT Mask = KV.second;

    if (BB == X) continue;
    if (BB == IPDom) continue;

    if ((Mask & (Mask - 1)) == 0)
      continue;

    if (!DT.dominates(X, BB))
      continue;

    Out.push_back(BB);
  }

  return Out;
}
  // === New-PM Function pass ===
  PreservedAnalyses MarkReconvBBPass::run(Function &F, FunctionAnalysisManager &FAM) {
      auto &UI  = FAM.getResult<UniformityInfoAnalysis>(F);
      auto &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);
      auto &DT  = FAM.getResult<DominatorTreeAnalysis>(F);
      SmallVector<BasicBlock*, 32> DivergentBranches;
      for (auto &BB : F) {
        if (auto *Br = dyn_cast<BranchInst>(BB.getTerminator())) {
          // if (!Br->isConditional()) continue;
          if (UI.hasDivergentTerminator(BB)) {
            DivergentBranches.push_back(&BB);
            // llvm::dbgs() << "IRBB" << BB.getNumber() << " has diverged Branch\n";
            continue;
          }
          if (UI.isDivergent(Br->getCondition())) {
            DivergentBranches.push_back(&BB);
            // llvm::dbgs() << "IRBB" << BB.getNumber() << " has diverged Branch\n";
            continue;
          }
          if (!UI.isUniform(Br->getCondition())) {
            DivergentBranches.push_back(&BB);
            // llvm::dbgs() << "IRBB" << BB.getNumber() << " has diverged Branch\n";
            continue;
          }
        }
      }

      bool Changed = false;
      unsigned K = F.getContext().getMDKindID(MD_RECONV);
      for (auto *Db : DivergentBranches) {
        for (BasicBlock *R : divergeReconveBB(Db, DT, PDT)) {
          // llvm::dbgs() << " " << R->getName() << "\n";
          // llvm::dbgs() << "IRBB" << R->getNumber() << " is marked as reconverge point\n";
          R->getTerminator()->setMetadata(K, MDNode::get(F.getContext(), {}));
          Changed = true;
        }
      }
      return Changed ? PreservedAnalyses::none()
                    : PreservedAnalyses::allInSet<AllAnalysesOn<Function>>();
    }
} // namespace llvm
