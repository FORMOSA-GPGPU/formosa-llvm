#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAInsertPri"

namespace {
class RISCVFSAInsertPri : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;
  MachineDominatorTree *MDT;
  MachineLoopInfo *MLI;

public:
  static char ID;
  RISCVFSAInsertPri() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAInsertPri";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTree>();
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end of anonymouse namespace


char RISCVFSAInsertPri::ID = 0;


INITIALIZE_PASS_BEGIN(RISCVFSAInsertPri, DEBUG_TYPE,
                     "FSA update branch instructions by inserting fsa.pri instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(RISCVFSAInsertPri, DEBUG_TYPE,
                     "FSA update branch instructions by inserting fsa.pri instructions", false, false)

// char &llvm::RISCVFSAInsertPriPassID = RISCVFSAInsertPri::ID;

void RISCVFSAInsertPri::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // const HSASubtarget &ST = F.getSubtarget<HSASubtarget>();
  MDT = &getAnalysis<MachineDominatorTree>();
  MPDT = &getAnalysis<MachinePostDominatorTree>();
  MLI = &getAnalysis<MachineLoopInfo>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAInsertPri::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;

  llvm::dbgs() <<  "Running RISCVFSAInsertPri on function: " << MF.getName() << "\n";
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

          if(PDomNode && PDomNode->getIDom() == nullptr){
            llvm::dbgs() <<  "IPDOM is null\n";
            continue;
          }
          // dominator reachable BB 
          MachineBasicBlock *DRBB = NULL;
          if(DomNode && DomNode->getIDom())
            DRBB = DomNode->getIDom()->getBlock();
          // post dominator reachable BB
          MachineBasicBlock *PDRBB = PDomNode->getIDom()->getBlock();
          if(PDRBB == nullptr){
            llvm::dbgs() <<  "PDRBB is null\n";
            continue;
          }

          MachineInstr &PDRBB_First = PDRBB->front();

          auto InstrName = TII->getName(MI.getOpcode());
          MadeChange = true;


          // If the current BB appears to be inside a loop, try to insert fsa.pri.raise at IDom of the BB
          // to prevent priority saturation inside loop
          // Consider following mir:
          // loop:
          //  ....
          // BLT x5, x6, loop;
          // We should not insert pri raise right before BLT, for that would cause priority saturation
          // if the loop iteration for more than 63 times
          if(MLI->getLoopFor(&MBB)){
            if(DRBB){
              llvm::dbgs() << "Inserting pri raise for possibly loop\n";
              MachineInstr &DRBB_Last = DRBB->back();
              BuildMI(*DRBB, DRBB_Last, DRBB_Last.getDebugLoc(),
                TII->get(RISCV::FSA_PRI_RAISE));
              goto InsertLowerInst;
            } else {
              llvm::dbgs() << "Skip pri insertion for possibly loop cond " << InstrName << "in BasicBlock: " << MBB.getName() << "\n"; ;
              // Cannot find IDom, skip this branch cond of loop
              continue;
            }
          }
          // Insert a pri raise befor branch inst
          llvm::dbgs() << "Inserting pri raise before branch instruction: " << InstrName
                        << " in BasicBlock: " << MBB.getName() << "\n";
          BuildMI(MBB, MI, MI.getDebugLoc(), 
          TII->get(RISCV::FSA_PRI_RAISE));
InsertLowerInst:
          llvm::dbgs() <<  "Insert pri lower at " << PDRBB->getFullName() << "\n";
          // Insert fsa.pri.lower at the begining of IPDom
          BuildMI(*PDRBB, PDRBB_First, PDRBB_First.getDebugLoc(),
            TII->get(RISCV::FSA_PRI_LOWER));
      }
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVRISCVFSAInsertPriPass() {
  return new RISCVFSAInsertPri();
}
