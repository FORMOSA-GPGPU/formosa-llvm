#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <queue>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSACleanUp"

namespace {
class RISCVFSACleanUp : public MachineFunctionPass {
private:
// Target Reg info
const RISCVRegisterInfo *TRI;
const RISCVInstrInfo *TII;

public:
static char ID;
RISCVFSACleanUp() : MachineFunctionPass(ID) {}
void initialize(MachineFunction &F);
bool runOnMachineFunction(MachineFunction &MF) override;
StringRef getPassName() const override {
    return "RISCVFSACleanUp";
}
};
} // namespace

char RISCVFSACleanUp::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSACleanUp, DEBUG_TYPE,
                      "FSA handling raise/lower priority by moving and remove fsa.pri ",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSACleanUp, DEBUG_TYPE,
                      "FSA handling raise/lower priority by moving and remove fsa.pri ",
                     false, false)

void RISCVFSACleanUp::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSACleanUp::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSACleanUp on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  // Skip insertion only when opt level is not none
  bool AllowSkip = (MF.getTarget().getOptLevel() != CodeGenOptLevel::None);

  std::unordered_map<MachineBasicBlock*, int> MBBRaiseBeforeLowerCnt;
  std::unordered_map<MachineBasicBlock*, int> MBBRaiseCnt;
  std::unordered_map<MachineBasicBlock*, int> MBBLowerCnt;

  for (MachineBasicBlock &MBB: MF) {
    bool MetLower = false;
    int RaiseBeforeLowerCnt = 0;
    int RaiseCnt = 0;
    int LowerCnt = 0;
    for(auto &MI : MBB) {
        if (MI.getOpcode() == RISCV::FSA_PRI_RAISE) {
            RaiseCnt++;
            if(!MetLower)
                RaiseBeforeLowerCnt++;
        } else if (MI.getOpcode() == RISCV::FSA_PRI_LOWER) {
            MetLower = true;
            LowerCnt++;
        }
    }
    

    int raise_cnt = 0;
    int lower_cnt = 0;
    // for (auto &MI : MBB) {
    //     if (MI.getOpcode() == RISCV::FSA_PRI_RAISE) {
    //         raise_cnt++;
    //         MI.eraseFromParent();
    //     } else if (MI.getOpcode() == RISCV::FSA_PRI_LOWER) {
    //         lower_cnt++;
    //         MI.eraseFromParent();
    //     }
    // }
    
    for (auto &MI : llvm::make_early_inc_range(MBB)) {
        if (MI.getOpcode() == RISCV::FSA_PRI_RAISE) {
            raise_cnt++;
            MI.eraseFromParent();
        } else if (MI.getOpcode() == RISCV::FSA_PRI_LOWER) {
            lower_cnt++;
            MI.eraseFromParent();
        }
    }
    
// ! Decrease lower_cnt may cause wrong ctrl flow, keep un changed
    // int min_cnt = std::min(raise_cnt, lower_cnt);
    // raise_cnt -= min_cnt;
    // lower_cnt -= min_cnt;
    auto TermIt = MBB.getFirstTerminator();
    int HasSelfLoop = 0;
    for(MachineBasicBlock *MBBPred : MBB.successors()) {
        if(MBBPred == &MBB)
            HasSelfLoop = 1;
    }

    if (raise_cnt == lower_cnt && raise_cnt == 0) {
        int MBBPredCnt = MBB.pred_size();
        if((MBBPredCnt - HasSelfLoop) > 1)
            raise_cnt = lower_cnt = 1;
    }
    for(int i = 0; i < raise_cnt; i++) {
        BuildMI(MBB, TermIt, MBB.findDebugLoc(TermIt), TII->get(RISCV::FSA_PRI_RAISE));
        MadeChange = true;
    }

    for(int i = 0; i < lower_cnt; i++) {
        BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()), TII->get(RISCV::FSA_PRI_LOWER));
        MadeChange = true;
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSACleanUpPass() {
  return new RISCVFSACleanUp();
}