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
#define DEBUG_TYPE "RISCVFSADFIC"

namespace {
class RISCVFSADFIC : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  std::unordered_map<MachineBasicBlock *, int> MBBDFIC; // dominance frontier intersection count
  std::unordered_set<MachineBasicBlock *> ReconvMBBSet;
  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  MachineDominanceFrontier *MDF;
  int CurPri;
  bool MadeChange;
  bool hasFSABar(const MachineBasicBlock& MBB);

public:
  static char ID;
  RISCVFSADFIC() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  void addFSARaiseN(MachineBasicBlock *, int);
  void addFSALowerN(MachineBasicBlock *, int);

  StringRef getPassName() const override { return "RISCVFSADFIC"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineDominanceFrontier>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSADFIC::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSADFIC, DEBUG_TYPE,
    "FSA handling reconv priority by inserting fsa.pri "
    "instructions based on dom tree pri propagate, use argument "
    "-fsa-uni-pri to enable the pass",
    false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)

INITIALIZE_PASS_END(
    RISCVFSADFIC, DEBUG_TYPE,
    "FSA handling reconv priority by inserting fsa.pri "
    "instructions based on dom tree pri propagate, use argument "
    "-fsa-uni-pri to enable the pass",
    false, false)

void RISCVFSADFIC::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MDF = &getAnalysis<MachineDominanceFrontier>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  ReconvMBBSet.clear();
  MBBDFIC.clear();
  ExitMBBSet.clear();
  CurPri = 0;
  MadeChange = false;
}


void RISCVFSADFIC::addFSARaiseN(MachineBasicBlock *MBB, int N) {
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

void RISCVFSADFIC::addFSALowerN(MachineBasicBlock *MBB, int N) {
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

bool RISCVFSADFIC::hasFSABar(const MachineBasicBlock& MBB) {
  for (auto &MI : MBB) {
    if (MI.getOpcode() == RISCV::FSA_BAR){
      return true;
    }
  }
  return false;
}


bool RISCVFSADFIC::runOnMachineFunction(MachineFunction &MF) {
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
    MBBDFIC[&MBB] = 0;
  }

  for (MachineBasicBlock &MBB : MF) {
    auto It = MDF->find(&MBB);
    if (It == MDF->end()) {
      continue;
    }
    const MachineDominanceFrontier::DomSetType &DFSet = It->second;
    for (MachineBasicBlock *DF : DFSet) {
      MBBDFIC[DF]++;
      if (MBB.isSuccessor(DF) && !DF->isSuccessor(&MBB)) {
        ReconvMBBSet.insert(DF);
      }
    }
    if (MBB.succ_empty()) {
      ExitMBBSet.insert(&MBB);
    }
  }
  int MaxDFC = 0;
  
  for (auto *MBB : ReconvMBBSet) {
    if (hasFSABar(*MBB))
      continue;
    if (ExitMBBSet.count(MBB)) {
      MaxDFC += (MaxDFC == 0); // Prevent situation that exitMBB is only reconv point
      continue;
    }

    int DFC = MBBDFIC[MBB];
    MaxDFC = std::max(MaxDFC, DFC);
    addFSALowerN(MBB, DFC);
    addFSARaiseN(MBB, DFC);
  }

  if (MaxDFC > 0) {
    auto &EntryMBB = *MF.begin();
    addFSARaiseN(&EntryMBB, MaxDFC);
    for (auto *ExitMBB : ExitMBBSet) {
      printf("Access Exit BB%d\n", ExitMBB->getNumber());
      addFSALowerN(ExitMBB, MaxDFC);
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSADFICPass() { return new RISCVFSADFIC(); }