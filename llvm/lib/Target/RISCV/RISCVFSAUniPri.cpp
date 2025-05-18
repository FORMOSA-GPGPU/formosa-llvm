#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <queue>
using namespace llvm;
// Domiator tree priority propagation
#define DEBUG_TYPE "RISCVFSAUniPri"

namespace {
class RISCVFSAUniPri : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachineLoopInfo *MLI;
  std::unordered_map<MachineBasicBlock *, int> MBBPriMap;
  std::unordered_set<MachineBasicBlock *> DFMBBSet;
  std::unordered_set<MachineBasicBlock *> ReconvMBBSet;
  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  MachineDominatorTree *MDT;
  MachineDominanceFrontier *MDF;
  bool MadeChange = false;
public:
  static char ID;
  RISCVFSAUniPri() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  void updatePriority(MachineBasicBlock *MBB);
  void addFSARaiseN(MachineBasicBlock *, int);
  void addFSALowerN(MachineBasicBlock *, int);
  MachineBasicBlock* addMBBBetween(MachineBasicBlock *From, MachineBasicBlock *To,
    MachineFunction &MF);

  StringRef getPassName() const override {
    return "RISCVFSAUniPri";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineDominanceFrontier>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAUniPri::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAUniPri, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on dom tree pri propagate, use argument "
                      "-fsa-uni-pri to enable the pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)

INITIALIZE_PASS_END(RISCVFSAUniPri, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on dom tree pri propagate, use argument "
                      "-fsa-uni-pri to enable the pass",
                      false, false)

void RISCVFSAUniPri::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MDF = &getAnalysis<MachineDominanceFrontier>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  MBBPriMap.clear();
  DFMBBSet.clear();
  ReconvMBBSet.clear();
  MadeChange = false;
}

void RISCVFSAUniPri::addFSARaiseN(MachineBasicBlock *MBB, int N) {
    auto TermIt = MBB->getFirstInstrTerminator();
    BuildMI(*MBB, TermIt, MBB->findDebugLoc(TermIt),
     TII->get(RISCV::FSA_PRI_RAISE_N)).addImm(N);
}

void RISCVFSAUniPri::addFSALowerN(MachineBasicBlock *MBB, int N) {
    BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()), 
    TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(N);
}

MachineBasicBlock* RISCVFSAUniPri::addMBBBetween(MachineBasicBlock *From, MachineBasicBlock *To,
    MachineFunction &MF) {
    // Step 1: Create NewBB and insert it immediately after From
    MachineBasicBlock *NewBB = MF.CreateMachineBasicBlock();
    MF.insert(++MachineFunction::iterator(From), NewBB);

    // Step 2: Update From's terminators that point to To, and redirect them to NewBB
    for (MachineInstr &MI : llvm::make_early_inc_range(llvm::reverse(*From))) {
        // Only need to look at terminators at the end of the block, we iterate From in Reverse
        // order, so once we met non-Terminator, all following inst must Not be terminator
        if (!MI.isTerminator())
            break;

        if (MI.isBranch()) {
            for (unsigned i = 0; i < MI.getNumOperands(); ++i) {
            MachineOperand &Op = MI.getOperand(i);
                if (Op.isMBB() && Op.getMBB() == To) {
                    Op.setMBB(NewBB); // Redirect jump target to NewBB
                }
            }
        }
    }

    // Step 3: Update CFG successor list
    if (From->isSuccessor(To)) {
        From->replaceSuccessor(To, NewBB);
    } else {
        From->addSuccessor(NewBB);
    }
    NewBB->addSuccessor(To);

    // Step 4: Insert jump in NewBB to To
    BuildMI(*NewBB, NewBB->end(), DebugLoc(), TII->get(RISCV::JAL), RISCV::X0).addMBB(To);
    return NewBB;
}


bool RISCVFSAUniPri::runOnMachineFunction(MachineFunction &MF) {
  bool MadeChange = false;
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

  for(MachineBasicBlock &MBB: MF) {
    MBBPriMap[&MBB] = 0;
  }

  for(MachineBasicBlock &MBB: MF) {
    if (MBB.succ_size() > 1) {
        // Add raise in diverge MBB
        addFSARaiseN(&MBB, 1);

        // Update following Priority
        MBBPriMap[&MBB] += 1;
        if (MDT->getNode(&MBB)) {
            DomTreeNodeBase<MachineBasicBlock> *DomNodeOfMBB = MDT->getNode(&MBB);
            for (DomTreeNodeBase<MachineBasicBlock> *Child : *DomNodeOfMBB) {
                if(!Child)
                continue;
                MachineBasicBlock *ChildMBB = Child->getBlock();
                if (ChildMBB) {
                    MBBPriMap[ChildMBB] += 1;
                }
            }
        }

        // Record diverged MBB's DF
        auto It = MDF->find(&MBB);
        if (It != MDF->end()) {
            const MachineDominanceFrontier::DomSetType &DFSet = It->second;
            for (MachineBasicBlock *DF : DFSet) {
                DFMBBSet.insert(DF);
            }
        }
    }
    if (MBB.pred_size() > 1) {
        addFSALowerN(&MBB, 1);
        MBBPriMap[&MBB] -= 1;
        if (MDT->getNode(&MBB)) {
            DomTreeNodeBase<MachineBasicBlock> *DomNodeOfMBB = MDT->getNode(&MBB);
            for (DomTreeNodeBase<MachineBasicBlock> *Child : *DomNodeOfMBB) {
                if(!Child)
                continue;
                MachineBasicBlock *ChildMBB = Child->getBlock();
                if (ChildMBB) {
                    MBBPriMap[ChildMBB] -= 1;
                }
            }
        }
        // Record diverged MBB's DF
        auto It = MDF->find(&MBB);
        if (It != MDF->end()) {
            const MachineDominanceFrontier::DomSetType &DFSet = It->second;
            for (MachineBasicBlock *DF : DFSet) {
                DFMBBSet.insert(DF);
            }
        }
    }
    if (MBB.succ_empty()) {
        ExitMBBSet.insert(&MBB);
    }
  }

  for (auto *MBB : DFMBBSet) {
    int MaxPri = 0;
    for (auto *Pred : MBB->predecessors()) {
        if (MBBPriMap[Pred] > MaxPri)
            MaxPri = MBBPriMap[Pred];
    }

    for (auto *Pred : MBB->predecessors()) {
        if (MBBPriMap[Pred] < MaxPri) {
            int DiffPri = MaxPri - MBBPriMap[Pred];
            MachineBasicBlock *TargetMBB = Pred;
            if (DFMBBSet.count(Pred)) {
                TargetMBB = addMBBBetween(Pred, MBB, MF);
            }
            addFSARaiseN(TargetMBB, DiffPri);
            MBBPriMap[TargetMBB] = MaxPri;

            if (MDT->getNode(MBB)) {
                DomTreeNodeBase<MachineBasicBlock> *DomNodeOfMBB = MDT->getNode(MBB);
                for (DomTreeNodeBase<MachineBasicBlock> *Child : *DomNodeOfMBB) {
                    if(!Child)
                    continue;
                    MachineBasicBlock *ChildMBB = Child->getBlock();
                    if (ChildMBB) {
                        MBBPriMap[ChildMBB] = MaxPri;
                    }
                }
            }

        }
    }
  }

  for (auto *MBB : ExitMBBSet) {
    int Pri = MBBPriMap[MBB];
    if (Pri > 0)
        addFSALowerN(MBB, Pri);
    else if (Pri < 0)
        addFSARaiseN(MBB, Pri);
  }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAUniPriPass() {
  return new RISCVFSAUniPri();
}