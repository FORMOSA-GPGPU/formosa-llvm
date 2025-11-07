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
#include "llvm/CodeGen/TargetSchedule.h"
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
extern cl::opt<bool> FSASkipLoopHeader;
extern cl::opt<int> FSABBNum;
extern cl::opt<int> FSAInstrCnt;
namespace {
class RISCVFSAPostTopo : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  llvm::TargetSchedModel TSchedModel;
  bool HasInstLatencyInfo;
  MachineLoopInfo *MLI;
  MachineCycleInfo *MCI;
  MachinePostDominatorTree *MPDT;
  MachineBasicBlock::iterator afterNthMI(MachineBasicBlock &MBB, unsigned N);
  MachineBasicBlock::iterator firstPromising(MachineBasicBlock &MBB);

public:
  static char ID;
  RISCVFSAPostTopo() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  bool hasSelfLoop(const MachineBasicBlock &);
  bool canSkipFSABar(MachineFunction &, MachineBasicBlock &);
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
  const llvm::TargetSubtargetInfo &STI = MF.getSubtarget();
  TSchedModel.init(&STI);
  HasInstLatencyInfo = TSchedModel.hasInstrSchedModel();
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

// Return first promising pri insertion point
MachineBasicBlock::iterator RISCVFSAPostTopo::firstPromising(MachineBasicBlock &MBB) {
  const int SkipLoad = 2;
  auto FirstTerm = MBB.getFirstInstrTerminator();
  if (FirstTerm == MBB.begin())
    return FirstTerm;

  auto It = MBB.begin();

  // Check a load is load-from-memory but not load-immediate
  auto RealLoad = [](const MachineInstr &MI) {
    if (!MI.mayLoad())
      return false;

    unsigned Opc = MI.getOpcode();
    switch (Opc) {
      case RISCV::ADDI:
      case RISCV::LUI:
      case RISCV::ADDIW:
        return false;
      default:
        return true;
    }
  };

  // Skip at most 2 RealLoad
  if (RealLoad(*It)) {
    int Count = 0;
    for (; It != FirstTerm && Count < SkipLoad; ++It, ++Count) {
      if (!RealLoad(*It))
        break;
    }
  }

  auto Tail = It;
  while (Tail != FirstTerm && Tail->isInsideBundle() && std::next(Tail) != FirstTerm) {
    ++Tail;
  }

  auto InsertPt = std::next(Tail);
  if (InsertPt == MBB.end())
    InsertPt = FirstTerm;

  return InsertPt;
}

bool RISCVFSAPostTopo::hasSelfLoop(const MachineBasicBlock& MBB) {
  for (MachineBasicBlock *SuccMBB : MBB.successors()) {
    if (SuccMBB == &MBB || (MBB.isPredecessor(SuccMBB) && FSASkipMutualLoop))
      return true;
  }
  return false;
}

bool RISCVFSAPostTopo::canSkipFSABar(MachineFunction& MF, MachineBasicBlock& MBB) {
  // If there has a write before barrier, force reconverge for better colescing opportunity
  bool NoWBeforBar = true;
  unsigned int CycleCost = canInsertSingleLower(MF, MBB) ? 1 : 2;
  unsigned int Latency = 0;
  for (auto &MI : MBB) {
    if (MI.mayStore())
      NoWBeforBar = false;
    if (MI.getOpcode() == RISCV::FSA_BAR){
      // Insert inst may induce 25% overhead (CycleCost / Latency >= 0.25)
      bool ShouldSkip = (4 * CycleCost) > Latency;
      return NoWBeforBar && ShouldSkip;
    }
    unsigned InstLat = TSchedModel.computeInstrLatency(&MI);
    Latency += InstLat ? InstLat : 1;
  }
  return false;
}

bool RISCVFSAPostTopo::canInsertSingleLower(MachineFunction &MF, MachineBasicBlock &MBB) {
  MachineBasicBlock *FunctionEntry = &MF.front();
  bool MBBPostDomEntry = MPDT->dominates(&MBB, FunctionEntry);
  bool MBBNotInCycle = MCI->getCycle(&MBB) == nullptr;
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
  bool SkipLoopHeader = FSASkipLoopHeader; // Skip insert in loop header and BBs having selfLoop
  bool TailInversion = false; // The raise of last pri pair (lower ... raise) is moved to entryBB if possible
  MachineBasicBlock *LastInsertBB;
  if(SkipLoopHeader) {
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
  }

  for(MachineBasicBlock &MBB: MF) {
    bool CanSkipLoop = hasSelfLoop(MBB) && (MBB.pred_size() < 3);
    // observing psort found that its not that meaningful to ensure reconv
    // at a reconverge point with following diverge point
    // TODO: Still insert lower when such BB has relevant large code body (i.e. inst > 8)
    if(MBB.pred_size() > MBB.succ_size()) {
      if (SkipLoopHeader) {
        if(!CanSkipLoop && !SkipInsertMBBSet.count(&MBB) && !canSkipFSABar(MF, MBB))
          ReconvBBSet.insert(&MBB);
      } else {
        if (!canSkipFSABar(MF, MBB))
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
      if(ReconvBBSet.count(MBB) && !canSkipFSABar(MF, *MBB)) {
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
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPostTopoPass() {
  return new RISCVFSAPostTopo();
}