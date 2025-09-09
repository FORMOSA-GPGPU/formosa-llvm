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
#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/CodeGen/MachinePostDominators.h"

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
extern cl::opt<bool> FSASkipMutualLoop;
extern cl::opt<int> FSABBNum;
extern cl::opt<int> FSAInstrCnt;
namespace {
class RISCVFSAPostTopo : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachineLoopInfo *MLI;
  MachineCycleInfo *MCI;
  MachinePostDominatorTree *MPDT;
  MachineBasicBlock::iterator afterNthMI(MachineBasicBlock &MBB, unsigned N);

public:
  static char ID;
  RISCVFSAPostTopo() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  bool hasSelfLoop(const MachineBasicBlock &);
  bool hasFSABar(MachineBasicBlock &);
  bool canInsertSingleLower(MachineFunction &, MachineBasicBlock &);
  StringRef getPassName() const override {
    return "RISCVFSAPostTopo";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineCycleInfoWrapperPass>();
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
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
INITIALIZE_PASS_DEPENDENCY(MachineCycleInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPostTopo, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on post order dfs, use argument "
                      "-fsa-post-topo to enable the pass",
                      false, false)

void RISCVFSAPostTopo::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MCI = &getAnalysis<MachineCycleInfoWrapperPass>().getCycleInfo();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

MachineBasicBlock::iterator RISCVFSAPostTopo::afterNthMI(MachineBasicBlock &MBB, unsigned N) {
  auto FirstTerm = MBB.getFirstInstrTerminator();
  if (N == 0) return MBB.begin();
  unsigned MICnt = 0;
  for(auto It = MBB.begin(); It != FirstTerm; ++It) {
    ++MICnt;
    if (MICnt == N) {
      auto Tail = It;
      // Keep going if Tail is inside bundel until FirstTerm
      while(Tail->isInsideBundle() && std::next(Tail) != FirstTerm) {
        ++Tail;
      }
      auto InsertPt = std::next(Tail);
      if(InsertPt == MBB.end())
        InsertPt = FirstTerm;
      return InsertPt;
    }
  }
  // There is no enough Instr inside given BB, return terminator
  return FirstTerm;
}

bool RISCVFSAPostTopo::hasSelfLoop(const MachineBasicBlock& MBB) {
  for (MachineBasicBlock *SuccMBB : MBB.successors()) {
    if (SuccMBB == &MBB || (MBB.isPredecessor(SuccMBB) && FSASkipMutualLoop))
      return true;
  }
  return false;
}

bool RISCVFSAPostTopo::hasFSABar(MachineBasicBlock& MBB) {
  bool NoWBeforBar = true;
  for (auto &MI : MBB) {
    if (MI.mayStore())
      NoWBeforBar = false;
    if (MI.getOpcode() == RISCV::FSA_BAR){
      MBB.splice(MBB.begin(), &MBB, MI.getIterator());
      if (NoWBeforBar) {
        llvm::dbgs() << "No W before Bar in BB" << MBB.getNumber() << "\n\n";
        for (auto &MI : MBB) {
          llvm::dbgs() << MI;
        }
        llvm::dbgs() << "\n\n";
      }
      return NoWBeforBar;
    }
  }
  return false;
}

bool RISCVFSAPostTopo::canInsertSingleLower(MachineFunction &MF, MachineBasicBlock &MBB) {
  MachineBasicBlock *FunctionEntry = &MF.front();
  bool MBBPostDomEntry = MPDT->dominates(&MBB, FunctionEntry);
  bool MBBNotInCycle = MCI->getCycle(&MBB) == nullptr;
  // llvm::dbgs() << "MBB" << MBB.getNumber() << " PostDomEntry = " << MBBPostDomEntry << "\n";
  // llvm::dbgs() << "MBB" << MBB.getNumber() << " NotInCycle = " << MBBNotInCycle << "\n";
  llvm::dbgs() << "MBB" << MBB.getNumber() << " CanInsertSingle = " << (MBBNotInCycle && MBBPostDomEntry) << "\n";

  return MBBPostDomEntry && MBBNotInCycle;
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
  bool InsertInExit = true;  
  bool SkipLoopHeader = true; // Skip insert in loop header and BBs having selfLoop
  bool TailInversion = false; // The raise of last pri pair (lower ... raise) is moved to entryBB if possible
  llvm::dbgs() << "Running RISCVFSAPostTopo on function: " << MF.getName() << "\n";
  MachineBasicBlock *LastInsertBB;
  for (auto *Loop : *MLI) {
    auto *MBB = Loop->getHeader();
    if (MBB && MBB->pred_size() <= 2)
      SkipInsertMBBSet.insert(MBB);

    for (auto *SubLoop : *Loop) {
      auto *MBB = SubLoop->getHeader();
      if (MBB && MBB->pred_size() <= 2)
        SkipInsertMBBSet.insert(MBB);
    }
  }

  for(MachineBasicBlock &MBB: MF) {
    bool CanSkip = hasSelfLoop(MBB) && (MBB.pred_size() < 3);
    if(MBB.pred_size() > 1) {
      if (SkipLoopHeader) {
        if(!CanSkip && !SkipInsertMBBSet.count(&MBB) && !hasFSABar(MBB))
          ReconvBBSet.insert(&MBB);
      } else {
        if (!hasFSABar(MBB))
          ReconvBBSet.insert(&MBB);
      }
    }
  }
  int StartPri = 0;
  int GuardPriCnt = 0;
  int SingleLowCnt = 0;

  bool InsertInEntry = true;
  InsertInExit = InsertInEntry;

  if(ReconvBBSet.size()) {
    for(auto *MBB: ReversePostOrderTraversal<MachineBasicBlock*>(&MF.front())) {
      if(MBB->succ_empty()) {
        ExitMBBSet.insert(MBB);
      }
      auto InsertAfter = (FSABBNum == MBB->getNumber() && FSAInstrCnt >= 0) ? FSAInstrCnt : 0;
      if(ReconvBBSet.count(MBB) && !hasFSABar(*MBB)) {
        if (canInsertSingleLower(MF, *MBB)) {
          BuildMI(*MBB, afterNthMI(*MBB, InsertAfter), MBB->findDebugLoc(MBB->begin()),
            TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(1);
          SingleLowCnt++;
          LastInsertBB = MBB;
        } else {
          GuardPriCnt++;
          auto TermIt = MBB->getFirstInstrTerminator();
          BuildMI(*MBB, afterNthMI(*MBB, InsertAfter), MBB->findDebugLoc(MBB->begin()),
            TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(GuardPriCnt);
          BuildMI(*MBB, TermIt, MBB->findDebugLoc(TermIt),
            TII->get(RISCV::FSA_PRI_RAISE_N)).addImm(GuardPriCnt);
          LastInsertBB = nullptr;
        }
        MadeChange = true;
      }
    }
  }
  StartPri = GuardPriCnt + SingleLowCnt;
  MachineBasicBlock &EntryMBB = *MF.begin();
  if (InsertInEntry && StartPri > 0) {
    MadeChange = true;
    auto EntryTermIt = EntryMBB.getFirstTerminator();
    BuildMI(EntryMBB, EntryTermIt, EntryMBB.findDebugLoc(EntryTermIt),
      TII->get(RISCV::FSA_PRI_RAISE_N)).addImm(StartPri);
  }
  int NeedLower = GuardPriCnt;
  if (InsertInExit && NeedLower > 0) {
    for (MachineBasicBlock *MBB : ExitMBBSet) {
      auto InsertAfter = (FSABBNum == MBB->getNumber() && FSAInstrCnt >= 0) ? FSAInstrCnt : 0;
      if (ReconvBBSet.count(MBB)) {
        int RaisedPri = 0;
        int Delta = NeedLower;
        for (auto &MI : llvm::make_early_inc_range(*MBB)) {
          auto OpCode = MI.getOpcode();
          if (OpCode == RISCV::FSA_PRI_RAISE_N) {
            RaisedPri = MI.getOperand(0).getImm();
            Delta = NeedLower - RaisedPri;
            // We simply remove the last pri raise inst since RaisedPri <= StartPri always hold
            MI.eraseFromParent();
          }
        }
        if (Delta != 0) { // We need to add delta to existed PriLower
          for (auto &MI : *MBB) {
            auto OpCode = MI.getOpcode();
            if (OpCode == RISCV::FSA_PRI_LOWER_N) {
              int LoweredPri = MI.getOperand(0).getImm();
              MI.getOperand(0).setImm(LoweredPri + Delta);
              Delta = 0;
            }
          }
          if (Delta != 0) {
            BuildMI(*MBB, afterNthMI(*MBB, InsertAfter), MBB->findDebugLoc(MBB->begin()),
              TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(Delta);
          }
        }
      } else {
        BuildMI(*MBB, afterNthMI(*MBB, InsertAfter), MBB->findDebugLoc(MBB->begin()),
          TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(NeedLower);
      }
      MadeChange = true;
    }
  }
  if (SingleLowCnt - 1 > 0)
    llvm::dbgs() << "Save " << SingleLowCnt - 1 << " pri raise inst\n\n";
  else
    llvm::dbgs() << "\n\n";
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPostTopoPass() {
  return new RISCVFSAPostTopo();
}