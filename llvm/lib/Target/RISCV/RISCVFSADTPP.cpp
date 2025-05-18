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
#include "llvm/CodeGen/MachineDominators.h"

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
// Domiator tree priority propagation
#define DEBUG_TYPE "RISCVFSADTPP"

namespace {
class RISCVFSADTPP : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachineLoopInfo *MLI;
  std::unordered_set<MachineBasicBlock *> ReconvMBBSet;
  std::unordered_map<MachineBasicBlock *, int> MBBPriMap;
  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  std::unordered_set<MachineBasicBlock *> SkipInsertMBBSet;
  MachineDominatorTree *MDT;
  bool InsertInEntry = false;
  bool LoopOpt = true; // Skip insert in loop header and BBs having selfLoop
  int CurrPri = 0;
public:
  static char ID;
  RISCVFSADTPP() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  bool hasSelfLoop(const MachineBasicBlock &);
  bool hasFSABar(const MachineBasicBlock &);
  void updatePriority(MachineBasicBlock *MBB);
  void addGuardPriInst(MachineBasicBlock *, int);

  StringRef getPassName() const override {
    return "RISCVFSADTPP";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSADTPP::ID = 0;


INITIALIZE_PASS_BEGIN(RISCVFSADTPP, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on dom tree pri propagate, use argument "
                      "-fsa-dtpp to enable the pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)

INITIALIZE_PASS_END(RISCVFSADTPP, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on dom tree pri propagate, use argument "
                      "-fsa-dtpp to enable the pass",
                      false, false)

void RISCVFSADTPP::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  MBBPriMap.clear();
  ReconvMBBSet.clear();
  ExitMBBSet.clear();
  SkipInsertMBBSet.clear();
  CurrPri = 0;
}

bool RISCVFSADTPP::hasSelfLoop(const MachineBasicBlock& MBB) {
  for (MachineBasicBlock *SuccMBB : MBB.successors()) {
    if (SuccMBB == &MBB)
      return true;
  }

  return false;
}

bool RISCVFSADTPP::hasFSABar(const MachineBasicBlock& MBB) {
  for (auto &MI : MBB) {
    if (MI.getOpcode() == RISCV::FSA_BAR){
      return true;
    }
  }
  return false;
}

inline void RISCVFSADTPP::addGuardPriInst(MachineBasicBlock *MBB, int GuardPri) {
    // Insert pri.lower in begining
    BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()), 
    TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(GuardPri);
  
    // Skip last pri.raise, succ_size() < 2 to ensure MBB has no back-edge (to loop header)
    // if (TailInversion && MBB->succ_size() < 2 && // MBB is exit
    //   GuardPri == static_cast<int>(ReconvMBBSet.size())) {
    //   InsertInEntry = true;
    //   return;
    // }
  
    // Insert pri.raise in terminater
    auto TermIt = MBB->getFirstInstrTerminator();
    BuildMI(*MBB, TermIt, MBB->findDebugLoc(TermIt),
     TII->get(RISCV::FSA_PRI_RAISE_N)).addImm(GuardPri);
}

void RISCVFSADTPP::updatePriority(MachineBasicBlock *MBB) {
  if (ReconvMBBSet.count(MBB)) {
    CurrPri++;
    MBBPriMap[MBB] = CurrPri > MBBPriMap[MBB] ? CurrPri : MBBPriMap[MBB];
  }
    
  for (auto *Succ: MBB->successors()) {
    if (MBBPriMap.count(Succ))
      MBBPriMap[Succ] = CurrPri > MBBPriMap[Succ] ? CurrPri : MBBPriMap[Succ];
    else
      MBBPriMap[Succ] = CurrPri;
  }
    
  if (MDT->getNode(MBB)) {
    DomTreeNodeBase<MachineBasicBlock> *DomNodeOfMBB = MDT->getNode(MBB);
    for (DomTreeNodeBase<MachineBasicBlock> *Child : *DomNodeOfMBB) {
      if(!Child)
        continue;
      MachineBasicBlock *ChildMBB = Child->getBlock();
      if (ChildMBB) {
        if (MBBPriMap.count(ChildMBB))
          MBBPriMap[ChildMBB] = CurrPri > MBBPriMap[ChildMBB] ? CurrPri : MBBPriMap[ChildMBB];
        else
          MBBPriMap[ChildMBB] = CurrPri;
      }
      updatePriority(ChildMBB);
    }
  }

  if (ReconvMBBSet.count(MBB))
    CurrPri--;
}

bool RISCVFSADTPP::runOnMachineFunction(MachineFunction &MF) {
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

  for (auto *Loop : *MLI) {
    // std::cout << "Found top-level loop Header: MBB" << Loop->getHeader()->getNumber() << " with following BBs:\n";
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
      if (LoopOpt) {
        if(!CanSkip && !SkipInsertMBBSet.count(&MBB) && !hasFSABar(MBB))
          ReconvMBBSet.insert(&MBB);
      } else {
        if (!hasFSABar(MBB))
          ReconvMBBSet.insert(&MBB);
      }
    }
    if (MBB.succ_empty()) {
      ExitMBBSet.insert(&MBB);
    }
  }

  for (MachineBasicBlock *MBB : ReconvMBBSet) {
    MBBPriMap[MBB] = 0;
  }
  MachineBasicBlock &EntryMBB = *MF.begin();
  MBBPriMap[&EntryMBB] = 0;
  updatePriority(&EntryMBB);
  // Filtered out BB which is not in ReconvMBBSet
  std::vector<std::pair<MachineBasicBlock*, int>> FilteredMBBPri;
  for (const auto &Ele : MBBPriMap) {
      if (ReconvMBBSet.count(Ele.first)) {
          FilteredMBBPri.push_back(Ele);
      }
  }
  // sort FilteredMBBPri by priority value
  std::sort(FilteredMBBPri.begin(), FilteredMBBPri.end(),
      [](const auto &a, const auto &b) {
          return a.second < b.second;  // ascending order
      });
  int Priority = 0;
  int LastValue = -1;
  for (const auto &Ele : FilteredMBBPri) {
      int TmpValue = Ele.second;
      if (TmpValue != LastValue) {
          Priority++;
          LastValue = TmpValue;
      }
      addGuardPriInst(Ele.first, Priority);
      MadeChange = true;
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSADTPPPass() {
  return new RISCVFSADTPP();
}