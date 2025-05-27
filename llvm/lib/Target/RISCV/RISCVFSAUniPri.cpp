#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <queue>
#include <unordered_map>
#include <unordered_set>
using namespace llvm;
// Domiator tree priority propagation
#define DEBUG_TYPE "RISCVFSAUniPri"

namespace {
class RISCVFSAUniPri : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachineLoopInfo *MLI;
  std::unordered_map<MachineBasicBlock *, int> MBBPriMap;
  std::unordered_map<MachineBasicBlock *, int> MBBFinalPriMap;
  std::unordered_map<MachineBasicBlock *, int> MBBPriDelta;
  std::unordered_set<MachineBasicBlock *> DFMBBSet;
  std::unordered_set<MachineBasicBlock *> ReconvMBBSet;
  std::unordered_set<MachineBasicBlock *> PriDeltaMBBSet;
  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  std::queue<MachineBasicBlock *> PriDeltaMBBQueue;
  // std::queue<MachineBasicBlock *> DFMBBQ;
  // TODO, add a bool var to set weather pass tend to insert raise or lower
  // instruction
  MachineDominatorTree *MDT;
  MachineDominanceFrontier *MDF;
  int CurPri;
  bool MadeChange;

public:
  static char ID;
  RISCVFSAUniPri() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  void updatePriority(MachineBasicBlock *MBB);
  void addFSARaiseN(MachineBasicBlock *, int);
  void addFSALowerN(MachineBasicBlock *, int);
  void updateDominatedPri(MachineBasicBlock *, int);
  void setDominatedPri(MachineBasicBlock *, int);
  MachineBasicBlock *addMBBBetween(MachineBasicBlock *From,
                                   MachineBasicBlock *To, MachineFunction &MF);

  StringRef getPassName() const override { return "RISCVFSAUniPri"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineDominanceFrontier>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAUniPri::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSAUniPri, DEBUG_TYPE,
    "FSA handling reconv priority by inserting fsa.pri "
    "instructions based on dom tree pri propagate, use argument "
    "-fsa-uni-pri to enable the pass",
    false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)

INITIALIZE_PASS_END(
    RISCVFSAUniPri, DEBUG_TYPE,
    "FSA handling reconv priority by inserting fsa.pri "
    "instructions based on dom tree pri propagate, use argument "
    "-fsa-uni-pri to enable the pass",
    false, false)

void RISCVFSAUniPri::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MDF = &getAnalysis<MachineDominanceFrontier>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  MBBPriMap.clear();
  DFMBBSet.clear();
  ReconvMBBSet.clear();
  MBBPriDelta.clear();
  MBBFinalPriMap.clear();
  PriDeltaMBBSet.clear();
  PriDeltaMBBQueue = {};
  CurPri = 0;
  MadeChange = false;
}

void RISCVFSAUniPri::setDominatedPri(MachineBasicBlock *RootMBB, int SetVal) {
  SmallVector<MachineBasicBlock *, 8> DominatedBBs;
  MDT->getDescendants(RootMBB, DominatedBBs);
  printf("Update pri for root MBB%d:\n", RootMBB->getNumber());
  for (auto *DBB : DominatedBBs) {
    printf("MBB%d: %d -> ", DBB->getNumber(), MBBPriMap[DBB]);
    MBBPriMap[DBB] = SetVal;
    printf("%d\n", SetVal);
  }
}

void RISCVFSAUniPri::updateDominatedPri(MachineBasicBlock *RootMBB, int Delta) {
  SmallVector<MachineBasicBlock *, 8> DominatedBBs;
  MDT->getDescendants(RootMBB, DominatedBBs);
  printf("Update pri for root MBB%d:\n", RootMBB->getNumber());
  for (auto *DBB : DominatedBBs) {
    printf("MBB%d: %d -> ", DBB->getNumber(), MBBPriMap[DBB]);
    MBBPriMap[DBB] += Delta;
    printf("%d\n", MBBPriMap[DBB]);
  }
}

void RISCVFSAUniPri::addFSARaiseN(MachineBasicBlock *MBB, int N) {
  for (auto &MI : llvm::make_early_inc_range(*MBB)) {
    if (MI.getOpcode() == RISCV::FSA_PRI_RAISE_N) {
      N += MI.getOperand(0).getImm();
      MI.eraseFromParent();
    }
  }
  auto TermIt = MBB->getFirstInstrTerminator();
  BuildMI(*MBB, TermIt, MBB->findDebugLoc(TermIt),
          TII->get(RISCV::FSA_PRI_RAISE_N))
      .addImm(N);
  MadeChange = true;
}

void RISCVFSAUniPri::addFSALowerN(MachineBasicBlock *MBB, int N) {
  for (auto &MI : llvm::make_early_inc_range(*MBB)) {
    if (MI.getOpcode() == RISCV::FSA_PRI_LOWER_N) {
      N += MI.getOperand(0).getImm();
      MI.eraseFromParent();
    }
  }
  BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
          TII->get(RISCV::FSA_PRI_LOWER_N))
      .addImm(N);
  MadeChange = true;
}

MachineBasicBlock *RISCVFSAUniPri::addMBBBetween(MachineBasicBlock *From,
                                                 MachineBasicBlock *To,
                                                 MachineFunction &MF) {


  // llvm::dbgs() << "From BB is BB" << From->getNumber() << "\n";
  // llvm::dbgs() << "To BB is BB" << To->getNumber() << "\n";

  // Step 1: Create and insert NewBB
  MachineBasicBlock *NewBB = MF.CreateMachineBasicBlock();
  MF.insert(MF.end(), NewBB);

  // Step 2: Modify From's terminators: redirect only To -> NewBB
  for (MachineInstr &MI : llvm::reverse(*From)) {
    if (!MI.isTerminator())
      break;

    if (MI.isBranch()) {
      // llvm::dbgs() << "found MI: " << MI << "\n";
      for (unsigned i = 0; i < MI.getNumOperands(); ++i) {
        MachineOperand &Op = MI.getOperand(i);
        // llvm::dbgs() << Op << "\n";
        if (Op.isMBB() && Op.getMBB() == To) {
          // llvm::dbgs() << "Found To BB: " << Op << "\n";
          Op.setMBB(NewBB); // Only redirect B
          // llvm::dbgs() << "After modified: " << Op << "\n";
        }
      }
      // llvm::dbgs() << "After modified of MI: " << MI << "\n\n";
    }
  }

  // Step 3: Carefully update CFG successor edges
  // From->splitSuccessor(To, NewBB, true);
  From->removeSuccessor(To); // Remove edge From->To
  From->addSuccessor(NewBB); // Add edge From->NewBB
  NewBB->addSuccessor(To);   // Add edge NewBB->To

  // Ensure propagate liveins From->NewBB
  for (auto &LI : To->liveins())
    NewBB->addLiveIn(LI);

  // Step 4: Add unconditional branch from NewBB->To
  SmallVector<MachineOperand, 0> EmptyCond;
  TII->insertBranch(*NewBB, To, nullptr, EmptyCond, DebugLoc());

  MadeChange = true;
  return NewBB;
}


bool RISCVFSAUniPri::runOnMachineFunction(MachineFunction &MF) {
  llvm::dbgs() << "----------------UniPri is called----------------\n";
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

  for (MachineBasicBlock &MBB : MF) {
    MBBPriMap[&MBB] = 0;
    MBBPriDelta[&MBB] = 0;
    MBBFinalPriMap[&MBB] = 0;
  }

  for (MachineBasicBlock &MBB : MF) {
    int Delta = 0;
    if (MBB.succ_size() > 1) {
      // Add raise in diverge MBB
      addFSARaiseN(&MBB, 1);
      Delta += 1;
    }
    
    if (MBB.pred_size() > 1) {
      // Add lower in reconverge MBB
      addFSALowerN(&MBB, 1);
      Delta -= 1;
    }

    if (Delta != 0) {
      PriDeltaMBBSet.insert(&MBB);
      MBBPriDelta[&MBB] += Delta;
      PriDeltaMBBQueue.push(&MBB);
      updateDominatedPri(&MBB, Delta);
    }

    if (MBB.succ_empty()) {
      ExitMBBSet.insert(&MBB);
    }
  }
  std::unordered_set<MachineBasicBlock *> Visited;
  std::list<MachineBasicBlock*> DFMBBQ;
  std::unordered_map<MachineBasicBlock*, std::list<MachineBasicBlock*>::iterator> DFMBBQMap;
  while(!PriDeltaMBBQueue.empty()) {
    auto *DBB = PriDeltaMBBQueue.front();
    PriDeltaMBBQueue.pop();
    if (!Visited.count(DBB)) {
      Visited.insert(DBB);
      auto It = MDF->find(DBB);
      if (It == MDF->end()) {
        llvm::report_fatal_error("Cannot find dominance frontier of MBB");
      }
      const MachineDominanceFrontier::DomSetType &DFSet = It->second;
      for (MachineBasicBlock *DF : DFSet) {
        auto it = DFMBBQMap.find(DF);
        if (it != DFMBBQMap.end()) {
          DFMBBQ.erase(it->second);
        }
        DFMBBQ.push_back(DF);
        DFMBBQMap[DF] = std::prev(DFMBBQ.end());
        PriDeltaMBBQueue.push(DF);
      }
    }
  }
  printf("DFMBB order:\n");
  for (auto *MBB : DFMBBQ) {
    printf("MBB%d\n", MBB->getNumber());
  }

  while(!DFMBBQ.empty()) {
    auto *DBB = DFMBBQ.front();
    DFMBBQ.pop_front();
    int MaxDelta = INT_MIN;
    for (auto *Pred : DBB->predecessors()) {
      MaxDelta = std::max(MaxDelta, MBBPriMap[Pred]);
    }
    printf("Access MBB%d with pri is %d\n", DBB->getNumber(), MBBPriMap[DBB]);
    for (auto *Pred : DBB->predecessors()) {
      int Delta = MaxDelta - MBBPriMap[Pred];
      if (Delta > 0) {
        if (Pred->succ_size() > 1) {
          auto *NewBB = addMBBBetween(Pred, DBB, MF);
          addFSARaiseN(NewBB, Delta);
        } else {
          addFSARaiseN(Pred, Delta);
          MBBPriMap[Pred] += Delta;
        }
      }
    }
    int PriDelta = MaxDelta - MBBPriMap[DBB];
    // setDominatedPri(DBB, MaxDelta);
    updateDominatedPri(DBB, PriDelta);
    printf("After update, now MBB%d has pri %d\n", DBB->getNumber(), MBBPriMap[DBB]);
  }


  // for (auto *MBB : PriDeltaMBBSet) {
  //   auto It = MDF->find(MBB);
  //   if (It == MDF->end()) {
  //     llvm::report_fatal_error("Cannot find dominance frontier of MBB");
  //   }
  //   const MachineDominanceFrontier::DomSetType &DFSet = It->second;
  //   for (MachineBasicBlock *DF : DFSet) {
  //     DFMBBSet.insert(DF);
  //   }
  // }

  // for (auto *MBB : DFMBBSet) {
  //   int MaxDelta = INT_MIN;
  //   for (auto *Pred : MBB->predecessors()) {
  //     MaxDelta = std::max(MaxDelta, MBBPriMap[Pred]);
  //   }
  //   for (auto *Pred : MBB->predecessors()) {
  //     int Delta = MaxDelta - MBBPriMap[Pred];
  //     if (Delta) {
  //       if (Pred->succ_size() > 1) {
  //         auto *NewBB = addMBBBetween(Pred, MBB, MF);
  //         addFSARaiseN(NewBB, Delta);
  //         MBBPriDelta[NewBB] = Delta;
  //       } else {
  //         addFSARaiseN(Pred, Delta);
  //         MBBPriDelta[Pred] += Delta;
  //       }

  //       SmallVector<MachineBasicBlock *, 8> DominatedBBs;
  //       MDT->getDescendants(MBB, DominatedBBs);
  //       for (auto *DBB : DominatedBBs) {
  //         MBBPriMap[DBB] += Delta;
  //       }
  //     }
  //   }
  // }

  printf("Pri map:\n");
  for (auto &MBB : MF) {
    printf("MBB%d: %d\n", MBB.getNumber(), MBBPriMap[&MBB]);
  }
  // printf("Pri delta map\n");
  // for (auto &MBB : MF) {
  //   printf("MBB%d: %d\n", MBB.getNumber(), MBBPriDelta[&MBB]);
  // }

  // std::queue<MachineBasicBlock *> MBBQ;
  // MachineBasicBlock &EntryMBB = *MF.begin();
  // MBBFinalPriMap[&EntryMBB] = MBBPriDelta[&EntryMBB];
  // MBBQ.push(&EntryMBB);
  // while (!MBBQ.empty()) {
  //   auto *MBB = MBBQ.front();
  //   MBBQ.pop();
  //   int CurPri = MBBFinalPriMap[MBB];
  //   for (auto *Succ : MBB->successors()) {
  //     if (!MBBFinalPriMap.count(Succ)) {
  //       MBBFinalPriMap[Succ] = CurPri + MBBPriDelta[Succ];
  //       MBBQ.push(Succ);
  //     }
  //   }
  // }

  // printf("Pri map final:\n");
  // for (auto &MBB : MF) {
  //   printf("MBB%d: %d\n", MBB.getNumber(), MBBFinalPriMap[&MBB]);
  // }

  for (auto *ExitMBB : ExitMBBSet) {
    if (MBBPriMap[ExitMBB] > 0) {
      addFSALowerN(ExitMBB, MBBPriMap[ExitMBB]);
    } else if (MBBPriMap[ExitMBB] < 0) {
      addFSARaiseN(ExitMBB, MBBPriMap[ExitMBB]);
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAUniPriPass() { return new RISCVFSAUniPri(); }