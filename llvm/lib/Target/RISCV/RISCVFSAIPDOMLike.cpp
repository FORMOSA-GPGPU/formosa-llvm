#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

#include <iterator>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAIPDOMLike"

namespace {
class RISCVFSAIPDOMLike : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;
  MachineLoopInfo *MLI;

public:
  static char ID;
  RISCVFSAIPDOMLike() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "RISCVFSAIPDOMLike"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAIPDOMLike::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSAIPDOMLike, DEBUG_TYPE,
    "FSA divergence handling by inserting fsa.pri instructions, "
    "use argument -fsa-ipdom-like to enable the pass",
    false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(
    RISCVFSAIPDOMLike, DEBUG_TYPE,
    "FSA divergence handling by inserting fsa.pri instructions, "
    "use argument -fsa-ipdom-like to enable the pass",
    false, false)

void RISCVFSAIPDOMLike::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAIPDOMLike::runOnMachineFunction(MachineFunction &MF) {
  int InsertPair = 0;
  std::set<MachineLoop *> MLoopSet{nullptr};
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAIPDOMLike on function: " << MF.getName()
             << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  MachineFunction::iterator NextBB;
  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end(); BI != BE;
       BI = NextBB) {
    NextBB = std::next(BI);
    MachineBasicBlock &MBB = *BI;
    MachineBasicBlock::iterator I, Next;
    for (I = MBB.getFirstTerminator(); I != MBB.end(); I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;
      if (MI.isConditionalBranch()) {
        DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
        auto InstrName = TII->getName(MI.getOpcode());
        LLVM_DEBUG(dbgs() << "Locate branch inst " << InstrName
                          << " in BasicBlock " << MBB.getName() << "\n";);

        // post dominator reachable BB
        MachineBasicBlock *PDRBB = nullptr;
        if (!(PDomNode && PDomNode->getIDom() &&
              (PDRBB = PDomNode->getIDom()->getBlock()))) {
          LLVM_DEBUG(dbgs() << "Cannot locate IPDOM, skip pri insertion\n\n";);
          continue;
        }
        /* clang-format off */ 
        /*
        // If the current BB appears to be inside a loop, try to insert fsa.pri.raise.t
        // at PreHeader of the BB to prevent priority saturation inside loop
        // Consider following mir:
        //    loop:
        //        ....
        //    BLT x5, x6, loop;
        // We should not insert pri.raise.t right before BLT, for that would cause priority
        // saturation if the loop iteration for a lot of times, the following section
        // is trying to deal it with following order when we find a loop:
        //    1. A set MLoopSet with only a nullptr inside it when init.
        //    2. When Identify a MachineLoop ML, do following:
        //        * If ML is not inside MLoopSet, this is a ML we haven't meet before,
        //          try to locate PreHeader (The BB immediate before the loop) of ML and insert
        //          pri.raise.t at the begining of PreHeader (We cannot insert at end of preheader, 
        //          cause we may insert inst. after the terminator of that BB, and may cause such 
        //          inst. to be a dead code which won't ever be executed.)
        //          While locating PreHeader, we also check following:
        //          2.1 If PreHeader is inside another loop L2, try find a PDom node N satisfied following:
        //            * Init: pd = IPDom(PreHeader)
        //            * keep evaluate {pd = IPDom(pd)} will eventually let pd become node N
        //            * N is inside the same loop L2. 
        //            If N is found, isnert lower. Otherwise skip handle this loop
        //          2.2 If PreHeader isn't inside any loop, try find a PDom node pd satisfied following:
        //            * Init: pd = IPDom(PreHeader)
        //            * keep evaluate {pd = IPDom(pd)} when pd is in any loop
        //        * If we cannot locate the PreHeader of ML, simply skip this loop.
        //        * No matter the PreHeader is located or not, insert ML into MLoopSet.
        //        * If ML is inside MLoopSet, means this is a ML we have already try to guarded it by 
        //          pri.raise.t, pri.lower.t before, goto 3. for other processing.
        //    3. If cond branch is inside loop, try find a PDom inside the same loop.
        //          If founded, insert pri.raise/lower.t. Else skip the branch
        //          We also need to ensure that PDom->getBlock() won't be MBB itself.
        */
        /* clang-format on */

        if (MachineLoop *ML = MLI->getLoopFor(&MBB)) {
          LLVM_DEBUG(dbgs() << "Current branch is inside loop\n";);
          // Current Loop is a new identified loop (Cannot find in MLoopSet)
          if (!MLoopSet.count(ML)) {
            // 2. try to locate the Loop
            MachineBasicBlock *PreHeader = MLI->findLoopPreheader(ML);
            MLoopSet.insert(ML);
            if (PreHeader) {
              MachineInstr &LoopHeadFisrt = PreHeader->front();
              MachineLoop *PreHeadMLoop = MLI->getLoopFor(PreHeader);
              // 2.1 PreHeader inside another loop L2, try find IPDom inside L2
              //  loop L2:
              //            <- PreHeader
              //    loop L1:
              //        ...
              //    end_loop L1
              //            <- L1's IPDom
              //  end_loop L2
              if (PreHeadMLoop) {
                LLVM_DEBUG(
                    dbgs()
                        << "PreHeader of current loop is also in another loop, "
                           "try "
                           "find suitable PDom in same loop of PreHeader\n";);
                while (PDomNode) {
                  if (MLI->getLoopFor(PDomNode->getBlock()) == PreHeadMLoop)
                    break;
                  PDomNode = PDomNode->getIDom();
                  LLVM_DEBUG(dbgs() << "Finding next PDom in block "
                                    << MBB.getName() << "\n";);
                }
              } else {
                // 2.2 preHead is not in loop, IPDom shouldn't be in loop either
                LLVM_DEBUG(dbgs() << "IPDom is in loop while PreHeader "
                                     "of loop is not\n";);
                while (PDomNode && MLI->getLoopFor(PDomNode->getBlock())) {
                  PDomNode = PDomNode->getIDom();
                  LLVM_DEBUG(dbgs() << "Finding next PDom in block "
                                    << MBB.getName() << "\n";);
                }
              }
              // Cannot find a suitable IPDom
              if (!PDomNode || !(PDRBB = PDomNode->getBlock())) {
                LLVM_DEBUG(dbgs() << "Cannot find suitable PDom, skip "
                                     "pri insertion\n\n";);
                continue;
              }
              // Else: insert pri
              LLVM_DEBUG(dbgs() << "Inserting pri.raise.t at begining of "
                                   "loopPreHeader\n";);
              BuildMI(*PreHeader, LoopHeadFisrt, LoopHeadFisrt.getDebugLoc(),
                      TII->get(RISCV::FSA_PRI_RAISE));
              goto InsertLowerInst;
            } else {
              LLVM_DEBUG(dbgs()
                             << "Cannot find preheader of loop in BasicBlock: "
                             << MBB.getName() << ", skip the loop\n\n";);
              continue;
            }
          }

          // 3. If cond branch is inside loop, try find a PDom inside the same
          // loop If founded, insert pri.raise/lower.t. Else skip the branch We
          // also need to ensure that PDom->getBlock() won't be MBB itself.
          do {
            PDRBB = PDomNode->getBlock();
            PDomNode = PDomNode->getIDom();
          } while (PDomNode && ML != MLI->getLoopFor(PDRBB));

          if (PDRBB != &MBB) {
            LLVM_DEBUG(
                dbgs() << "Inserting pri.raise.t before branch instruction\n";);
            BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::FSA_PRI_RAISE));
            goto InsertLowerInst;
          } else {
            LLVM_DEBUG(dbgs() << "Cannot find a suitable PDom of branch for "
                                 "insertion, skip the branch inside loop\n\n";);
            continue;
          }
        }
        // Insert a pri.raise.t befor branch inst if MBB is not in loop
        LLVM_DEBUG(
            dbgs() << "Inserting pri.raise.t before branch instruction\n";);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::FSA_PRI_RAISE));
      InsertLowerInst:
        MachineInstr &PDRBBFirst = PDRBB->front();
        LLVM_DEBUG(dbgs() << "Insert pri.lower.t at " << PDRBB->getFullName()
                          << "\n\n";);
        // Insert fsa.pri.lower.t at the begining of IPDom
        BuildMI(*PDRBB, PDRBBFirst, PDRBBFirst.getDebugLoc(),
                TII->get(RISCV::FSA_PRI_LOWER));
        MadeChange = true;
        ++InsertPair;
      }
    }
  }
  LLVM_DEBUG(dbgs() << InsertPair
                    << " pair(s) of pri.raise/lower.t on function: "
                    << MF.getName() << "\n\n\n";);
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAIPDOMLikePass() {
  return new RISCVFSAIPDOMLike();
}
