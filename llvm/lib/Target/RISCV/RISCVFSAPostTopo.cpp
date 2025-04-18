#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <queue>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPostTopo"

namespace {
class RISCVFSAPostTopo : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;

public:
  static char ID;
  RISCVFSAPostTopo() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAPostTopo";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
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
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPostTopo, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on post order dfs, use argument "
                      "-fsa-post-topo to enable the pass",
                      false, false)

void RISCVFSAPostTopo::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPostTopo::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPostTopo on function: "
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

  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  std::unordered_set<MachineBasicBlock *> ReconvBBSet;
  bool InsertInExit = false;
  for(MachineBasicBlock &MBB: MF) {
    bool IsExitMBB = MBB.succ_empty();
    if (IsExitMBB){
      ExitMBBSet.insert(&MBB);
      if(MBB.pred_size() > 1)
        InsertInExit = true;
      continue;
    }

    if (MBB.pred_size() > 1) {
      ReconvBBSet.insert(&MBB);
    }
  }
  int StartPri = 1 + InsertInExit;

  if(ReconvBBSet.size()) {
    for(auto *MBB: post_order(&MF.front())) {
      if(ReconvBBSet.count(MBB)) {
        LLVM_DEBUG(dbgs() << "Visit BB" << MBB->getNumber(); << "\n");
        BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
        TII->get(RISCV::FSA_PRI_SET)).addImm(StartPri);
        StartPri++;
        MadeChange = true;
      }
    }
  }

  MachineBasicBlock &EntryMBB = *MF.begin();
  bool InsertInEntry = (EntryMBB.succ_size() > 1 && MadeChange) || (!MadeChange && InsertInExit);
  if (InsertInEntry) {
    MadeChange = true;
    auto EntryTermIt = EntryMBB.getFirstTerminator();
    BuildMI(EntryMBB, EntryTermIt, EntryMBB.findDebugLoc(EntryTermIt),
      TII->get(RISCV::FSA_PRI_SET)).addImm(StartPri);
  }

  if (InsertInExit) {
    for (MachineBasicBlock *MBB : ExitMBBSet) {
      BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
        TII->get(RISCV::FSA_PRI_SET)).addImm(1);
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPostTopoPass() {
  return new RISCVFSAPostTopo();
}