#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/ADT/PostOrderIterator.h"

#include <unordered_set>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPostTopo"
namespace {
class RISCVFSAPostTopo : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVInstrInfo *TII;
  MachineBasicBlock::iterator blockBeginInsertPt(MachineBasicBlock &MBB);
  bool shouldSkipInsertion(MachineBasicBlock &MBB);
  void insertLower(MachineBasicBlock &MBB, unsigned Pri);
  void insertRaise(MachineBasicBlock &MBB, unsigned Pri);

public:
  static char ID;
  RISCVFSAPostTopo() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAPostTopo";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
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
INITIALIZE_PASS_END(RISCVFSAPostTopo, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on post order dfs, use argument "
                      "-fsa-post-topo to enable the pass",
                      false, false)



void RISCVFSAPostTopo::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
}

MachineBasicBlock::iterator
RISCVFSAPostTopo::blockBeginInsertPt(MachineBasicBlock &MBB) {
  return MBB.SkipPHIsLabelsAndDebug(MBB.begin());
}

bool RISCVFSAPostTopo::shouldSkipInsertion(MachineBasicBlock &MBB) {
  auto CountUntilBarOrTerm = [this](MachineBasicBlock &Block, bool &HitBar) {
    unsigned Count = 0;
    HitBar = false;
    for (auto It = blockBeginInsertPt(Block), End = Block.end(); It != End;
         ++It) {
      MachineInstr &MI = *It;
      if (MI.getOpcode() == RISCV::FSA_BAR) {
        HitBar = true;
        break;
      }
      if (MI.isTerminator())
        break;
      ++Count;
    }
    return Count;
  };

  bool HitBar = false;
  unsigned Count = CountUntilBarOrTerm(MBB, HitBar);
  if (HitBar)
    return Count <= 2;

  if (MBB.succ_size() == 1) {
    MachineBasicBlock *SuccMBB = *MBB.succ_begin();
    if (SuccMBB != &MBB) {
      bool SuccHitBar = false;
      Count += CountUntilBarOrTerm(*SuccMBB, SuccHitBar);
    }
  }
  return Count <= 2;
}

void RISCVFSAPostTopo::insertLower(MachineBasicBlock &MBB, unsigned Pri) {
  auto InsertPt = blockBeginInsertPt(MBB);
  BuildMI(MBB, InsertPt, MBB.findDebugLoc(InsertPt),
          TII->get(RISCV::FSA_PRI_LOWER_N))
      .addImm(Pri);
}

void RISCVFSAPostTopo::insertRaise(MachineBasicBlock &MBB, unsigned Pri) {
  auto InsertPt = MBB.getFirstInstrTerminator();
  BuildMI(MBB, InsertPt, MBB.findDebugLoc(InsertPt),
          TII->get(RISCV::FSA_PRI_RAISE_N))
      .addImm(Pri);
}

bool RISCVFSAPostTopo::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPostTopo on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
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

  std::unordered_set<MachineBasicBlock *> ReconvBBSet;
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.pred_size() > 1 && !shouldSkipInsertion(MBB))
      ReconvBBSet.insert(&MBB);
  }

  if (ReconvBBSet.empty())
    return false;

  unsigned NextPri = 1;
  for (MachineBasicBlock *MBB :
       ReversePostOrderTraversal<MachineBasicBlock *>(&MF.front())) {
    if (!ReconvBBSet.count(MBB))
      continue;
    insertLower(*MBB, NextPri);
    insertRaise(*MBB, NextPri);
    ++NextPri;
  }

  unsigned EntryExitPri = NextPri;
  MachineBasicBlock &EntryMBB = MF.front();
  auto EntryInsertPt = EntryMBB.getFirstTerminator();
  BuildMI(EntryMBB, EntryInsertPt, EntryMBB.findDebugLoc(EntryInsertPt),
          TII->get(RISCV::FSA_PRI_RAISE_N))
      .addImm(EntryExitPri);

  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.succ_empty())
      continue;
    insertLower(MBB, EntryExitPri);
  }

  return true;
}

FunctionPass *llvm::createRISCVFSAPostTopoPass() {
  return new RISCVFSAPostTopo();
}
