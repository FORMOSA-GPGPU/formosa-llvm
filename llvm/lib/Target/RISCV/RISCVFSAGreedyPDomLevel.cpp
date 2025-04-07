#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

#include <cstdint>
#include <iterator>
#include <sys/types.h>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAGreedyPDomLevel"

namespace {
class RISCVFSAGreedyPDomLevel : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;

public:
  static char ID;
  RISCVFSAGreedyPDomLevel() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAGreedyPDomLevel";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAGreedyPDomLevel::ID = 0;
uint64_t MaxReconvPri = 0;
uint64_t LastPri = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAGreedyPDomLevel, DEBUG_TYPE,
                      "FSA handling PDom priority by inserting fsa.pri "
                      "instructions based on PDom level, use argument "
                      "-fsa-pdom-level to enable the pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAGreedyPDomLevel, DEBUG_TYPE,
                    "FSA handling PDom priority by inserting fsa.pri "
                    "instructions based on PDom level, use argument "
                    "-fsa-pdom-level to enable the pass",
                    false, false)

void RISCVFSAGreedyPDomLevel::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAGreedyPDomLevel::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAGreedyPDomLevel on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
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


  unsigned PDomLevel = 0;
  for (MachineBasicBlock &MBB : MF) {
    DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
    if (!PDomNode) {
      LLVM_DEBUG(
          dbgs() << "Cannot find PDom node for current machine basic block "
                 << MBB.getName() << "\n";);
        PDomLevel = LastPri;
    //   continue;
    } else {
        PDomLevel = PDomNode->getLevel();
        LastPri = PDomLevel;
    }
    if(MBB.pred_size() > 1) {
        if(PDomLevel > MaxReconvPri) {
            MaxReconvPri = PDomLevel;
        }
    }
  }
  std::set<int> MBBSet;
  std::unordered_map<int, uint64_t> MBBPriMap;
  LastPri = 0;
  PDomLevel = 0;
  for (MachineBasicBlock &MBB : MF) {
    DomTreeNodeBase<MachineBasicBlock> *PDomNode = MPDT->getNode(&MBB);
    if (!PDomNode) {
      LLVM_DEBUG(
          dbgs() << "Cannot find PDom node for current machine basic block "
                 << MBB.getName() << "\n";);
        PDomLevel = LastPri;
    } else {
        PDomLevel = PDomNode->getLevel();
        LastPri = PDomLevel;
    }

    // set the priority based on the level of IDom
    LLVM_DEBUG(dbgs() << "BB " << MBB.getName() << " priority: " << PDomLevel
                      << "\n");
    if (PDomLevel > 63) {
      report_fatal_error("Number of PDom level exceeds 63, cannot insert "
                         "fsa.pri.set instructions");
    }
    uint64_t FinalPri = MaxReconvPri + 1;
    bool NeedInsertion = false;

    // If one of the successor is a reconv point,
    // we need to insert pri inst
    for(MachineBasicBlock *Succ: MBB.successors()) {
      if(Succ->pred_size() > 1) {
        NeedInsertion = true;
      }
    }

    int MBBNum = MBB.getNumber();
    // If current bb is a reconv point, we need to insert pri inst
    // with pri <= MaxReconvPri
    if(MBB.pred_size() > 1) {
      NeedInsertion = true;
      if(PDomLevel < MaxReconvPri)
        FinalPri = PDomLevel;
      else
        FinalPri = MaxReconvPri;
    } else {
      uint64_t PredPri = 0;
      if (MBB.pred_size() == 1) {
        MachineBasicBlock *PredMBB = MBB.getSinglePredecessor();
        int PredMBBNum = PredMBB->getNumber();
        bool FoundInSet = MBBSet.find(PredMBBNum) != MBBSet.end();
        while(!FoundInSet && PredMBB->pred_size() == 1) {
          PredMBB = PredMBB->getSinglePredecessor();
          PredMBBNum = PredMBB->getNumber();
          FoundInSet = MBBSet.find(PredMBBNum) != MBBSet.end();
        }
        
        // Get Pred's Number
        if(FoundInSet)
          PredPri = MBBPriMap[PredMBBNum];
        NeedInsertion = (PredPri != FinalPri);
      }
      uint64_t SuccPri = -1ULL;
      if(MBB.succ_size() == 1 && PredPri != 0) {
        MachineBasicBlock *SuccMBB = MBB.getSingleSuccessor();
        int SuccMBBNum = SuccMBB->getNumber();
        bool FoundInSet = MBBSet.find(SuccMBBNum) != MBBSet.end();
        while(!FoundInSet && SuccMBB->succ_size() == 1) {
          SuccMBB = SuccMBB->getSingleSuccessor();
          SuccMBBNum = SuccMBB->getNumber();
          FoundInSet = MBBSet.find(SuccMBBNum) != MBBSet.end();
        }

        // Get Succ's Number
        if(FoundInSet)
          SuccPri = MBBPriMap[SuccMBBNum];
        // Only need to insert if the priority of the successor is
        // greater than the priority of the predecessor in the pri
        // transit chain
        NeedInsertion = (SuccPri > PredPri);
      }
    }

    // at exit point, give the lowest priority
    if(MBB.succ_size() == 0) {
      NeedInsertion = true;
      FinalPri = 1;
    }
    
    if(NeedInsertion){
      BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
              TII->get(RISCV::FSA_PRI_SET))
              .addImm(FinalPri);
      MBBSet.insert(MBBNum);
      MBBPriMap[MBBNum] = FinalPri;
    }
    MadeChange = true;
  }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAGreedyPDomLevelPass() {
  return new RISCVFSAGreedyPDomLevel();
}