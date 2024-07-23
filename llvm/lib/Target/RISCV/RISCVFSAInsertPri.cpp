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
  int insert_pair = 0;
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  std::set<MachineLoop *> ML_set {nullptr};
  llvm::dbgs() << "-------------------------------------------------------------------\n";
  llvm::dbgs() <<  "Running RISCVFSAInsertPri on function: " << MF.getName() << "\n";
  llvm::dbgs() << "-------------------------------------------------------------------\n\n\n";

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
          auto InstrName = TII->getName(MI.getOpcode());

          llvm::dbgs() << "Locate branch inst " << InstrName << " in BasicBlock " << MBB.getName() << "\n";

          // post dominator reachable BB
          MachineBasicBlock *PDRBB = nullptr;
          if(!(PDomNode && PDomNode->getIDom() && (PDRBB = PDomNode->getIDom()->getBlock()))){
            llvm::dbgs() <<  "Cannot locate IPDOM, skip pri insertion\n\n";
            continue;
          }
          // dominator reachable BB 
          // MachineBasicBlock *DRBB = NULL;
          // if(DomNode && DomNode->getIDom())
          //   DRBB = DomNode->getIDom()->getBlock();

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
          //          While locating PreHeader, we also check following:
          //          2.1 If PreHeader is inside another loop L2, try find a PDom node N satisfied following:
          //            * Init: pd = IPDom(PreHeader)
          //            * keep evaluate {pd = IPDom(pd)} will eventually let pd become node N
          //            * N is inside the same loop L2. 
          //            If we cannot find such IPDom, skip handle this loop.
          //          2.2 If PreHeader isn't inside any loop, try find a PDom node pd satisfied following:
          //            * Init: pd = IPDom(PreHeader)
          //            * keep evaluate {pd = IPDom(pd)} when pd is not in any loop
          //        * If we cannot locate the PreHeader of ML, simply skip this loop.
          //        * No matter the PreHeader is located or not, insert ML into ML_set.
          //        * If ML is inside ML_set, means this is a ML we have already try to guarded it by 
          //          pri.raise, pri.lower before, goto 3. for other processing.
          //    3. If current cond branch is inside the loop. Check whether IPDom of cond branch is inside
          //       ths same loop. If so, add raise before the MBB and add lower at IPDom. Else skip this
          //       cond branch.
          */

          if(MachineLoop *ML = MLI->getLoopFor(&MBB)){
            llvm::dbgs() << "Current branch is inside loop\n";

            // Current Loop is a new identified loop (Cannot find in ML_set)
            if(!ML_set.count(ML)){
              // 2. try to locate the Loop
              MachineBasicBlock *preHeader = MLI->findLoopPreheader(ML);
              ML_set.insert(ML);
              if(preHeader){
                MachineInstr &LoopHead_fisrt = preHeader->front();
                MachineLoop *ML_preHead = MLI->getLoopFor(preHeader);
                // 2.1 preHeader inside another loop L2, try find IPDom inside L2
                //  loop L2:
                //            <- preHeader
                //    loop L1:
                //        ...
                //    end_loop L1
                //            <- L1's IPDom
                //  end_loop L2
                if(ML_preHead) {
                  llvm::dbgs() << "PreHeader of current loop is also in another loop, try find suitable PDom in same loop of preHeader\n";
                  while(PDomNode){
                    if(MLI->getLoopFor(PDomNode->getBlock()) == ML_preHead)
                      break;
                    PDomNode = PDomNode->getIDom();
                    llvm::dbgs() << "Finding next PDom in block "
                                  << MBB.getName() << "\n";
                  }
                } else {
                  // 2.2 preHead is not in loop, IPDom shouldn't be in loop either
                  llvm::dbgs() << "IPDom is in loop while preHeader of loop is not\n";
                  while(PDomNode && MLI->getLoopFor(PDomNode->getBlock())){
                    PDomNode = PDomNode->getIDom();
                  llvm::dbgs() << "Finding next PDom in block " << MBB.getName() << "\n";
                  }
                }
                // Cannot find a suitable IPDom
                if(!PDomNode || !(PDRBB = PDomNode->getBlock())){
                  llvm::dbgs() << "Cannot find suitable PDom, skip pri insertion\n\n";
                  continue;
                }
                // Else: insert pri
                llvm::dbgs() << "Inserting pri raise at begining of loopPreHeader\n";
                BuildMI(*preHeader, LoopHead_fisrt, LoopHead_fisrt.getDebugLoc(), 
                TII->get(RISCV::FSA_PRI_RAISE));
                goto InsertLowerInst;
              } else {
                llvm::dbgs() << "Cannot find preheader of loop in BasicBlock: " 
                              << MBB.getName() << ", skip the loop\n\n";
                continue;
              }
            }

            // 3. If cond branch is inside loop, insert pri.raise iff IPDom is inside same loop
            if (ML == MLI->getLoopFor(PDRBB)){
              llvm::dbgs() << "Inserting pri raise before branch instruction\n";
              BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::FSA_PRI_RAISE));
              goto InsertLowerInst;
            } else {
              llvm::dbgs() << "IPDom of branch is not in the same loop, skip insertion\n\n";
              continue;
            }
          }
          // Insert a pri raise befor branch inst if MBB is not in loop
          llvm::dbgs() << "Inserting pri raise before branch instruction\n";
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::FSA_PRI_RAISE));
InsertLowerInst:
          MachineInstr &PDRBB_First = PDRBB->front();
          llvm::dbgs() <<  "Insert pri lower at " << PDRBB->getFullName() << "\n\n";
          // Insert fsa.pri.lower at the begining of IPDom
          BuildMI(*PDRBB, PDRBB_First, PDRBB_First.getDebugLoc(),
            TII->get(RISCV::FSA_PRI_LOWER));
          MadeChange = true;
          ++insert_pair;
      }
    }
  }
  llvm::dbgs() << insert_pair << " pair(s) of pri raise/lower on function: " << MF.getName() << "\n\n\n";
  return MadeChange;
}

FunctionPass *llvm::createRISCVRISCVFSAInsertPriPass() {
  return new RISCVFSAInsertPri();
}
