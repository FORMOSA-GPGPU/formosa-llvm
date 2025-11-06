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

  if ((!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Divergence)) &&
      AllowSkip) {
    LLVM_DEBUG(dbgs() << "Function does not have divergence, skip priority "
                         "instructions insertion\n");
    return false;
  }
  std::unordered_map<MachineBasicBlock*, int> MBBRaiseCnt;
  std::unordered_map<MachineBasicBlock*, int> MBBLowerCnt;
  std::set<MachineInstr *> FSAPRISETSet;

  for (MachineBasicBlock &MBB: MF) {
    bool MetLower = false;
    int RaiseBeforeLowerCnt = 0;
    int RaiseCnt = 0;
    int LowerCnt = 0;
        // make_early_inc_range ensures we can safely remove instructions
        // (like with eraseFromParent) without invalidating the iterator.
        for(auto &MI : llvm::make_early_inc_range(MBB)) {
            if (MI.getOpcode() == RISCV::FSA_PRI_RAISE) {
                RaiseCnt++;
                if(!MetLower)
                    RaiseBeforeLowerCnt++;
                MI.eraseFromParent();
            } else if (MI.getOpcode() == RISCV::FSA_PRI_LOWER) {
                MetLower = true;
                LowerCnt++;
                MI.eraseFromParent();
            } else if (MI.getOpcode() == RISCV::FSA_PRI_SET) {
                FSAPRISETSet.insert(&MI);
            }
        }
        if (RaiseBeforeLowerCnt > LowerCnt) {
            RaiseCnt -= LowerCnt;
            LowerCnt = 0;
        } else {
            RaiseCnt -= RaiseBeforeLowerCnt;
            LowerCnt -= RaiseBeforeLowerCnt;
        }

        MBBRaiseCnt[&MBB] = RaiseCnt;
        MBBLowerCnt[&MBB] = LowerCnt;
    }

    for (MachineBasicBlock &MBB : MF) {
        auto TermIt = MBB.getFirstTerminator();
        int RaiseCnt = MBBRaiseCnt[&MBB];
        int LowerCnt = MBBLowerCnt[&MBB];
        if (RaiseCnt > 0)
        BuildMI(MBB, TermIt, MBB.findDebugLoc(TermIt), TII->get(RISCV::FSA_PRI_RAISE_N)).addImm(RaiseCnt);
        MadeChange = true;
        if (LowerCnt > 0)
        BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()), TII->get(RISCV::FSA_PRI_LOWER_N)).addImm(LowerCnt);
        MadeChange = true;
    }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSACleanUpPass() {
  return new RISCVFSACleanUp();
}