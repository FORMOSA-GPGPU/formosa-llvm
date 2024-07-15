#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
using namespace llvm;
#define DEBUG_TYPE "FSABranchOpt"

namespace {
class FSABranchOpt : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;
  MachineDominatorTree *MDT;

public:
  static char ID;
  FSABranchOpt() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "FSABranchOpt";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTree>();
    AU.addRequired<MachineDominatorTree>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end of anonymouse namespace


char FSABranchOpt::ID = 0;


INITIALIZE_PASS_BEGIN(FSABranchOpt, DEBUG_TYPE,
                     "FSA update branch instructions by inserting fsa.pri instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_END(FSABranchOpt, DEBUG_TYPE,
                     "FSA update branch instructions by inserting fsa.pri instructions", false, false)

// char &llvm::FSABranchOptPassID = FSABranchOpt::ID;

void FSABranchOpt::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // const HSASubtarget &ST = F.getSubtarget<HSASubtarget>();
  MDT = &getAnalysis<MachineDominatorTree>();
  MPDT = &getAnalysis<MachinePostDominatorTree>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool FSABranchOpt::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;

  llvm::dbgs() <<  "Running FSABranchOpt on function: " << MF.getName() << "\n";
  bool MadeChange = false;
  initialize(MF);
  MachineFunction::iterator NextBB;
  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end();
       BI != BE; BI = NextBB) {
    NextBB = std::next(BI);
    MachineBasicBlock &MBB = *BI;
    MachineBasicBlock::iterator I, Next;
    for (I = MBB.getFirstTerminator(); I != MBB.end(); I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;
      if(MI.isConditionalBranch()){
          DomTreeNodeBase<MachineBasicBlock> *DomNode = MDT->getNode(&MBB);
          DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
          if(PDomNode == nullptr || DomNode == nullptr){
            llvm::dbgs() <<  "Either Dom or PDom is null\n";
            llvm_unreachable("Unreachable Machine Dom/PDom Basic Block.");
          }

          if(DomNode->getIDom() == nullptr || PDomNode->getIDom() == nullptr){
            llvm::dbgs() <<  "Either tmp_drbb or tmp_pdrbb is null\n";
            continue;
          }

          // dominator reachable BB 
          MachineBasicBlock *DRBB = DomNode->getIDom()->getBlock();
          // post dominator reachable BB
          MachineBasicBlock *PDRBB = PDomNode->getIDom()->getBlock();

          if(DRBB == nullptr || PDRBB == nullptr){
            llvm::dbgs() <<  "Either DRBB or PDRBB is null\n";
            continue;
          }

          MachineInstr &DRBB_Last = DRBB->back();
          MachineInstr &PDRBB_First = PDRBB->front();

          MadeChange = true;
          llvm::dbgs() <<  "Insert pri raise at the end of " << DRBB->getFullName() << "\n";
          // Insert fsa.pri.raise at the end of IDom
          BuildMI(*DRBB, DRBB_Last, DRBB_Last.getDebugLoc(),
            TII->get(RISCV::FSA_PRI_RAISE));

          llvm::dbgs() <<  "Insert pri lower at the begin of " << PDRBB->getFullName() << "\n";
          // Insert fsa.pri.lower at the begin of IPDom
          BuildMI(*PDRBB, PDRBB_First, PDRBB_First.getDebugLoc(),
            TII->get(RISCV::FSA_PRI_LOWER));
      }
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSABranchOptPass() {
  return new FSABranchOpt();
}
