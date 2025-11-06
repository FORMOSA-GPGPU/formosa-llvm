#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/CodeGen/MachineLoopInfo.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <queue>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPostTopo"

namespace {
class RISCVFSAPostTopo : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachineLoopInfo *MLI;

public:
  static char ID;
  RISCVFSAPostTopo() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  bool hasSelfLoop(const MachineBasicBlock &);
  bool hasFSABar(const MachineBasicBlock &);
  StringRef getPassName() const override {
    return "RISCVFSAPostTopo";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAPostTopo::ID = 0;


INITIALIZE_PASS_BEGIN(RISCVFSAPostTopo, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on post order dfs, use argument "
                      "-fsa-post-topo to enable the pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)

INITIALIZE_PASS_END(RISCVFSAPostTopo, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on post order dfs, use argument "
                      "-fsa-post-topo to enable the pass",
                      false, false)

void RISCVFSAPostTopo::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPostTopo::hasSelfLoop(const MachineBasicBlock& MBB) {
  for (MachineBasicBlock *SuccMBB : MBB.successors()) {
    if (SuccMBB == &MBB)
      return true;
  }

  return false;
}

bool RISCVFSAPostTopo::hasFSABar(const MachineBasicBlock& MBB) {
  for (auto &MI : MBB) {
    if (MI.getOpcode() == RISCV::FSA_BAR){
      return true;
    }
  }
  return false;
}

bool RISCVFSAPostTopo::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPostTopo on function: "
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

  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  std::unordered_set<MachineBasicBlock *> ReconvBBSet;
  std::unordered_set<MachineBasicBlock *> LoopHeaderBBSet;
  std::unordered_map<MachineBasicBlock *, int> ReconvBBPri;
  std::unordered_set<MachineBasicBlock *> NeedInsertMBBSet;
  std::unordered_set<MachineBasicBlock *> SkipInsertMBBSet;
  bool InsertInExit = false;
  for (auto *Loop : *MLI) {
    bool LoopHasDiverge = false;
    int BackEdgeCnt = 0;
    // std::cout << "Found top-level loop Header: MBB" << Loop->getHeader()->getNumber() << " with following BBs:\n";
    for (auto *MBB : Loop->getBlocks()) {
      bool HasEdgeToHeader = false;
      bool HasEdgeFromHeader = false;
      if (MBB == Loop->getHeader())
        continue;
      std::cout << "MBB" << MBB->getNumber();
      for (auto *Succ : MBB->successors()) {
        if(Succ == Loop->getHeader()) {
          HasEdgeToHeader = true;
          BackEdgeCnt++;
          int MBBPredSize = MBB->pred_size();
          for (auto *Pred : MBB->predecessors()) {
            if (Pred == Loop->getHeader()) {
              HasEdgeFromHeader = true;
              MBBPredSize -= 1;
              break;
            }
          }
          if (MBBPredSize < 2) {
            SkipInsertMBBSet.insert(MBB);
          }
        }
      }
      std::cout << "\n";
      if (MBB->pred_size() - (HasEdgeFromHeader && HasEdgeToHeader) > 1) {
        LoopHasDiverge = true;
        // std::cout << "Loop Contain divergence at MBB" << MBB->getNumber() << "\n";
      }
    }

    if (!LoopHasDiverge) {
      if ((int)Loop->getHeader()->pred_size() - BackEdgeCnt > 2)
        NeedInsertMBBSet.insert(Loop->getHeader());
      else
        SkipInsertMBBSet.insert(Loop->getHeader());
    }

      // for (auto *SubLoop : *Loop)
      //  std::cout << "Found nested loop Header: " << SubLoop->getHeader()->getNumber() << "\n";
  }

  for(MachineBasicBlock &MBB: MF) {
    bool IsExitMBB = MBB.succ_empty();
    if (IsExitMBB){
      ExitMBBSet.insert(&MBB);
      if(MBB.pred_size() > 1)
        InsertInExit = true;
      continue;
    }
    
    if(MBB.pred_size() > 1) {
      if (hasFSABar(MBB))
        continue;
      if(!hasSelfLoop(MBB))
        ReconvBBSet.insert(&MBB);
      else if(MBB.pred_size() > 2)      // Except self loop edge, there still exist more than 1 pred
        NeedInsertMBBSet.insert(&MBB);  // Need insert new MBB to deal with
    }
  }
  int StartPri = InsertInExit;

  if (NeedInsertMBBSet.size()) {
    for (auto *MBB : NeedInsertMBBSet) {
      MachineBasicBlock *NewMBB = MF.CreateMachineBasicBlock();
      MF.insert(MBB->getIterator(), NewMBB);

      BuildMI(*NewMBB, NewMBB->end(), DebugLoc(), TII->get(TargetOpcode::G_BR)).addMBB(MBB);
      NewMBB->addSuccessor(MBB);

      // make_early_inc_range let us to modify MBB's preds safely
      for (auto *MBBPred : llvm::make_early_inc_range(MBB->predecessors())) {
        if (MBBPred == MBB) // Don't touch self loop edge
          continue;

        bool IsFallThroughBB = MBBPred->isLayoutSuccessor(MBB);

        if (IsFallThroughBB) {
          // Move NewMBB to fallthrough terminate point ?
          MF.splice(std::next(MBBPred->getIterator()), NewMBB);
          BuildMI(*MBBPred, MBBPred->end(), DebugLoc(), TII->get(TargetOpcode::G_BR)).addMBB(NewMBB);
        } else {
          for (MachineInstr &Term : *MBBPred) {
            if(!Term.isBranch()) continue;
            for (unsigned i = 0; i < Term.getNumOperands(); ++i) {
              MachineOperand &Op = Term.getOperand(i);
              // For all edge pointed to MBB, redirect to NewMBB;
              if (Op.isMBB() && Op.getMBB() == MBB) {
                Op.setMBB(NewMBB);
              }
            }
          }
        }
        MBBPred->removeSuccessor(MBB);
        MBBPred->addSuccessor(NewMBB);
      }
      ReconvBBSet.insert(NewMBB);
    }
  }

  if(ReconvBBSet.size()) {
    for(auto *MBB: ReversePostOrderTraversal<MachineBasicBlock*>(&MF.front())) {
      if(ReconvBBSet.count(MBB)) {
        if(SkipInsertMBBSet.count(MBB)) continue;
        auto TermIt = MBB->getFirstInstrTerminator();
        
        BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
        TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(StartPri);

        BuildMI(*MBB, TermIt, MBB->findDebugLoc(TermIt),
        TII->get(RISCV::FSA_PRI_RAISE_N)).addImm(StartPri);

        StartPri++;
        MadeChange = true;
      }
    }
  }

  MachineBasicBlock &EntryMBB = *MF.begin();
  bool InsertInEntry = (EntryMBB.succ_size() > 1 && MadeChange) || (!MadeChange && InsertInExit);
  if (InsertInEntry) {
    MadeChange = true;
    auto EntryTermIt = EntryMBB.getFirstTerminator();
    BuildMI(EntryMBB, EntryTermIt, EntryMBB.findDebugLoc(EntryTermIt),
      TII->get(RISCV::FSA_PRI_RAISE_N)).addImm(StartPri);
  }

  if (InsertInExit) {
    for (MachineBasicBlock *MBB : ExitMBBSet) {
      BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
        TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(StartPri);
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPostTopoPass() {
  return new RISCVFSAPostTopo();
}