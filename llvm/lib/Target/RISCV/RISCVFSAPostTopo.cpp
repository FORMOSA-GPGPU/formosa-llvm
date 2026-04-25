#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/ADT/PostOrderIterator.h"

#include <unordered_set>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPostTopo"
extern cl::opt<bool> FSASkipLoopHeader;
extern cl::opt<bool> FSASkipSmallBB;
namespace {
class RISCVFSAPostTopo : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVInstrInfo *TII;
  MachineLoopInfo *MLI;
  MachineBasicBlock::iterator blockBeginInsertPt(MachineBasicBlock &MBB);
  bool shouldSkipInsertion(MachineBasicBlock &MBB);
  std::unordered_set<MachineBasicBlock *> collectSkipLoopHeaders();
  std::unordered_set<MachineBasicBlock *>
  collectReconvBlocks(MachineFunction &MF,
                      const std::unordered_set<MachineBasicBlock *> &SkipMBBs);
  unsigned insertLocalPriorityPairs(MachineFunction &MF,
                                    const std::unordered_set<MachineBasicBlock *> &ReconvBBs);
  void insertEntryExitCompensation(MachineFunction &MF, unsigned Pri);
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
}

MachineBasicBlock::iterator
RISCVFSAPostTopo::blockBeginInsertPt(MachineBasicBlock &MBB) {
  return MBB.SkipPHIsLabelsAndDebug(MBB.begin());
}

bool RISCVFSAPostTopo::shouldSkipInsertion(MachineBasicBlock &MBB) {
  if (!FSASkipSmallBB)
    return false;

  auto CountUntilTerm = [this](MachineBasicBlock &Block) {
    unsigned Count = 0;
    for (auto It = blockBeginInsertPt(Block), End = Block.end(); It != End;
         ++It) {
      MachineInstr &MI = *It;
      if (MI.isTerminator())
        break;
      ++Count;
    }
    return Count;
  };

  unsigned Count = CountUntilTerm(MBB);
  if (MBB.succ_size() == 1) {
    MachineBasicBlock *SuccMBB = *MBB.succ_begin();
    if (SuccMBB != &MBB)
      Count += CountUntilTerm(*SuccMBB);
  }
  return Count <= 2;
}

std::unordered_set<MachineBasicBlock *>
RISCVFSAPostTopo::collectSkipLoopHeaders() {
  std::unordered_set<MachineBasicBlock *> SkipMBBs;
  if (!FSASkipLoopHeader)
    return SkipMBBs;

  for (auto *Loop : *MLI) {
    auto *MBB = Loop->getHeader();
    if (MBB && MBB->pred_size() <= 2)
      SkipMBBs.insert(MBB);

    for (auto *SubLoop : *Loop) {
      auto *SubMBB = SubLoop->getHeader();
      if (SubMBB && SubMBB->pred_size() <= 2)
        SkipMBBs.insert(SubMBB);
    }
  }
  return SkipMBBs;
}

std::unordered_set<MachineBasicBlock *>
RISCVFSAPostTopo::collectReconvBlocks(
    MachineFunction &MF,
    const std::unordered_set<MachineBasicBlock *> &SkipMBBs) {
  std::unordered_set<MachineBasicBlock *> ReconvBBs;
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.pred_size() > 1 && !SkipMBBs.count(&MBB) &&
        !shouldSkipInsertion(MBB))
      ReconvBBs.insert(&MBB);
  }
  return ReconvBBs;
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

unsigned RISCVFSAPostTopo::insertLocalPriorityPairs(
    MachineFunction &MF,
    const std::unordered_set<MachineBasicBlock *> &ReconvBBs) {
  unsigned NextPri = 1;
  for (MachineBasicBlock *MBB :
       ReversePostOrderTraversal<MachineBasicBlock *>(&MF.front())) {
    if (!ReconvBBs.count(MBB))
      continue;
    // Exit blocks only receive entry/exit compensation, not local reconv pairs.
    if (MBB->succ_empty())
      continue;
    insertLower(*MBB, NextPri);
    insertRaise(*MBB, NextPri);
    ++NextPri;
  }
  return NextPri;
}

void RISCVFSAPostTopo::insertEntryExitCompensation(MachineFunction &MF,
                                                   unsigned Pri) {
  MachineBasicBlock &EntryMBB = MF.front();
  auto EntryInsertPt = EntryMBB.getFirstTerminator();
  BuildMI(EntryMBB, EntryInsertPt, EntryMBB.findDebugLoc(EntryInsertPt),
          TII->get(RISCV::FSA_PRI_RAISE_N))
      .addImm(Pri);

  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.succ_empty())
      continue;
    insertLower(MBB, Pri);
  }
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

  std::unordered_set<MachineBasicBlock *> SkipInsertMBBSet =
      collectSkipLoopHeaders();
  std::unordered_set<MachineBasicBlock *> ReconvBBSet =
      collectReconvBlocks(MF, SkipInsertMBBSet);

  if (ReconvBBSet.empty())
    return false;

  unsigned EntryExitPri = insertLocalPriorityPairs(MF, ReconvBBSet);
  insertEntryExitCompensation(MF, EntryExitPri);

  return true;
}

FunctionPass *llvm::createRISCVFSAPostTopoPass() {
  return new RISCVFSAPostTopo();
}
