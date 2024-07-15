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
#include <cstdint>
#include <iterator>
using namespace llvm;
#define DEBUG_TYPE "RISCVOptFallBB"
namespace {
class RISCVOptFallBB : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;

public:
  static char ID;
  RISCVOptFallBB() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVOptFallBB";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // end of anonymouse namespace

char RISCVOptFallBB::ID = 0;


INITIALIZE_PASS_BEGIN(RISCVOptFallBB, DEBUG_TYPE,
                     "FSA update branch instructions by removing unneeded branch", false, false)
INITIALIZE_PASS_END(RISCVOptFallBB, DEBUG_TYPE,
                     "FSA update branch instructions by removing unneeded branch", false, false)

void RISCVOptFallBB::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // const HSASubtarget &ST = F.getSubtarget<HSASubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVOptFallBB::runOnMachineFunction(MachineFunction &MF) {
  llvm::dbgs() <<  "Running RISCVOptFallBB on function: " << MF.getName() << "\n";
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
      if(MI.isUnconditionalBranch()){
        MachineBasicBlock *TBB = TII->getBranchDestBlock(MI);
        
        // FBB is the basic block that will be executed if we ignore the branch
        MachineBasicBlock *FBB = MBB.getFallThrough();
        
        // If the target basic block (TBB) of the unconditional branch is the same as the fall-through basic block (FBB),
        // remove the branch instruction to optimize the code because whether branch or not, we will execute the same BB(FBB).
        if(TBB == FBB){
          llvm::dbgs() <<  "Erase on function: " << MF.getName() << "\n";
          I->eraseFromParent();
          MadeChange = true;
        }
      }
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVOptFallBBPass() {
  return new RISCVOptFallBB();
}