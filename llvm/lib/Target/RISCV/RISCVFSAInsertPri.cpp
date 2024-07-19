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
  std::set<MachineLoop *> ML_set {nullptr};
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
            // TODO: if PDRBB is null, find branch target and insert lower at the end of that target
            continue;
          }

          MachineInstr &PDRBB_First = PDRBB->front();

          auto InstrName = TII->getName(MI.getOpcode());
          MadeChange = true;

          /*
          // If the current BB appears to be inside a loop, try to insert fsa.pri.raise
          // at PreHeader of the BB to prevent priority saturation inside loop
          // Consider following mir:
          //    loop:
          //        ....
          //    BLT x5, x6, loop;
          // We should not insert pri.raise right before BLT, for that would cause priority
          // saturation if the loop iteration for a lot of times, the following section
          // is trying to deal it with following order when we find a loop:
          //    1. A set ML_set with only a nullptr inside it when init.
          //    2. When Identify a MachineLoop ML, do following:
          //        * If ML is not inside ML_set, this is a ML we haven't meet before,
          //          try to locate PreHeader (The BB immediate before the loop) of ML and insert
          //          pri.raise at the begining of PreHeader (We cannot insert at end of preheader, 
          //          cause we may insert inst. after the terminator of that BB, and may cause such 
          //          inst. to be a dead code which won't ever be executed.)
          //        * If cannot locate the PreHeader of ML, we simply skip this loop.
          //        * No matter the PreHeader is located or not, insert ML into ML_set.
          //        * If ML is inside ML_set, means this is a ML we have already try to guarded it by 
          //          pri.raise, pri.lower before, goto 3. for other processing.
          //    3. If current branch is inside the loop. Check if the cond branch's
          //       IPDom is inside the same for loop (there has no return/break statement). If
          //       true, add raise before the inst and add lower at IPDom.
          //    4. Current branch's IPDom is outside loop, simply skip the branch
          */
          if(MachineLoop *ML = MLI->getLoopFor(&MBB)){
              // Current Loop is a new identified loop (Cannot find in ML_set)
              if(!ML_set.count(ML)){
                // 2. try to locate the Loop
                MachineBasicBlock *preHeader = MLI->findLoopPreheader(ML);
                ML_set.insert(ML);
                if(preHeader){
                  MachineInstr &LoopHead_fisrt = preHeader->front();
                  llvm::dbgs() << "Inserting pri raise before loop in BasicBlock: " 
                                << MBB.getName() << "\n";
                  BuildMI(*preHeader, LoopHead_fisrt, LoopHead_fisrt.getDebugLoc(), 
                  TII->get(RISCV::FSA_PRI_RAISE));
                  goto InsertLowerInst;
                } else {
                  llvm::dbgs() << "Cannot find preheader of loop in BasicBlock: " 
                                << MBB.getName() << ", skip the loop\n";
                  continue;
                }
              }

              // * if current branch is in the loop ctrl block
              // means current branch is the exit cond of loop
              // insert pri.raise before loop if IDom exist (DRBB != nullptr)
              // We can assure that an IPDom of LoopCtrlBlock would always 
              // outside the loop, so we won't need to handle the situation
              // that there has no IDom (goto else if block) while LCBB's 
              // IPDom is inside loop and cause priority saturation for misinserting
              // fsa.pri.raise
              
              // findLoopControlBlock Will return NULL if there has multiple leave point
              // of the Loop, so This way is impractical

              // TODO: use findLoopPreheader to insert raise in preHeader?
              // We can use BranchTarget to check if a cond branch will leave the loop
              // However, if there are multiple exit point, we shouldn't add multiple
              // raise/lower
              // Perhaps try following:
              // * If BranchTarget will leave the loop
              // * Insert raise at IDom and lower at IPDom if IDom's first inst is not
              //   fsa.pri.raise (Haven't been insert pri inst for cur loop before)
              // * If cond branch's IPDom is outside the loop, skip the branch. Otherwise
              //   Insert raise before branch and lower at IPDom of the branch

            // MachineBasicBlock *LCBB = ML->findLoopControlBlock();
            // if(DRBB && LCBB && LCBB == &MBB){ // Compare unique number to ensure LCBB == MBB
            //   llvm::dbgs() << "Inserting pri raise before loop\n";
            //   MachineInstr &DRBB_Last = DRBB->front();
            //   BuildMI(*DRBB, DRBB_Last, DRBB_Last.getDebugLoc(),
            //     TII->get(RISCV::FSA_PRI_RAISE));
            //   goto InsertLowerInst;
            // } else 
            
            if(MLI->getLoopFor(&MBB) == MLI->getLoopFor(PDRBB)){
              // 3. When the cond branch's IPDom is inside the same for loop
              // and cond branch is not loop ctrl block
              // ( findLoopControlBlock() return false )
              // Add rasie before the inst and add lower at IPDom
              llvm::dbgs() << "Inserting pri raise before branch instruction: " << InstrName
                            << " in BasicBlock: " << MBB.getName() << "\n";
              BuildMI(MBB, MI, MI.getDebugLoc(), 
              TII->get(RISCV::FSA_PRI_RAISE));
              goto InsertLowerInst;
            } else {
              // 4. Skip the branch
              // llvm::dbgs() << "LCBB ptr is " << LCBB << "\n";
              llvm::dbgs() << "Skip pri insertion for possibly loop cond " << InstrName
              << "in BasicBlock: " << MBB.getName() << "\n"; ;
              continue;
            }
            // if(DRBB && MLI->getLoopFor(&MBB) == MLI->getLoopFor(PDRBB)){
            //   llvm::dbgs() << "Inserting pri raise for possibly loop\n";
            //   MachineInstr &DRBB_Last = DRBB->front();
            //   BuildMI(*DRBB, DRBB_Last, DRBB_Last.getDebugLoc(),
            //     TII->get(RISCV::FSA_PRI_RAISE));
            //   goto InsertLowerInst;
            // } else {
            //   llvm::dbgs() << "Skip pri insertion for possibly loop cond " << InstrName << "in BasicBlock: " << MBB.getName() << "\n"; ;
            //   // If IDom not exist or IPDom is outside the loop, skip this branch inside loop
            //   continue;
            // }
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
