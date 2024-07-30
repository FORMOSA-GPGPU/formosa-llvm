#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

#include <iterator>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPDomLvBasedPriority"
#define FSA_HIGHEST_PRI 8


namespace {
class RISCVFSAPDomLvBasedPriority : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;

public:
  static char ID;
  RISCVFSAPDomLvBasedPriority() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "RISCVFSAPDomLvBasedPriority"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTree>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAPDomLvBasedPriority::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSAPDomLvBasedPriority, DEBUG_TYPE,
    "FSA handling PDom priority by inserting fsa.pri instructions based on PDom level", false,
    false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_END(
    RISCVFSAPDomLvBasedPriority, DEBUG_TYPE,
    "FSA handling PDom priority by inserting fsa.pri instructions based on PDom level", false,
    false)

void RISCVFSAPDomLvBasedPriority::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTree>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPDomLvBasedPriority::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPDomLvBasedPriority on function: " << MF.getName()
             << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  MachineFunction::iterator NextBB;
  int base_priority = 0;
  // TODO: get base priority
  // RISCV::FSA_PRI_GET_BASE(&base_priority)
  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end(); BI != BE;
       BI = NextBB) {
    NextBB = std::next(BI);
    MachineBasicBlock &MBB = *BI;
    MachineBasicBlock::iterator I, Next;

    for (I = MBB.getFirstTerminator(); I != MBB.end(); I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;
      if(MI.isReturn()){
        // TODO: Perhaps memorize the priority when enter a function then set back to that priority when leave
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::FSA_PRI_RESET));
      } 

      DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
      if(!PDomNode){
        LLVM_DEBUG(dbgs() << "Cannot find IPDOM for current machine basic block " << MBB.getName() << "\n";);
      }
      int PDomLv = PDomNode->getLevel();
      if(PDomLv + base_priority > 31){ // 6bits signed int for priority, range in: -32 <= pri <= 31
        dbgs() << "priority overflow in function " << MF.getName() << ", perhaps the function has deeply nested branches?\n";
        LLVM_DEBUG(
          dbgs() << "Checkout MBB " << MBB.getName() << "\n";
        );
      }
      // TODO: set PDOM based priority
      // RISCV::FSA_PRI_SET(PDomLv + base_priority)
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPDomLvBasedPriorityPass() {
  return new RISCVFSAPDomLvBasedPriority();
}