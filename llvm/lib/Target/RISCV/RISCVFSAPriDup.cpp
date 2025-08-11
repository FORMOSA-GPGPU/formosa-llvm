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
#define DEBUG_TYPE "RISCVFSAPriDup"
extern cl::opt<int> FSAPriDupCount;

namespace {
class RISCVFSAPriDup : public MachineFunctionPass {
private:
bool shouldDuplicate(MachineInstr &);
// Target Reg info
const RISCVRegisterInfo *TRI;
const RISCVInstrInfo *TII;

public:
static char ID;
RISCVFSAPriDup() : MachineFunctionPass(ID) {}
void initialize(MachineFunction &F);
bool runOnMachineFunction(MachineFunction &MF) override;
StringRef getPassName() const override {
    return "RISCVFSAPriDup";
}
};
} // namespace

char RISCVFSAPriDup::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAPriDup, DEBUG_TYPE,
                      "Duplicate fsa.pri inst by FSAPriDupCount times",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPriDup, DEBUG_TYPE,
                      "Duplicate fsa.pri inst by FSAPriDupCount times",
                     false, false)

void RISCVFSAPriDup::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPriDup::shouldDuplicate(MachineInstr &MI) {
    auto OpCode = MI.getOpcode();
    return (OpCode == RISCV::FSA_PRI_RAISE_N || OpCode == RISCV::FSA_PRI_LOWER_N ||
        OpCode == RISCV::FSA_PRI_SET || OpCode == RISCV::FSA_PRI_RAISE ||
        OpCode == RISCV::FSA_PRI_LOWER);
}

bool RISCVFSAPriDup::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPriDup on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  initialize(MF);
  // Skip insertion only when opt level is not none

  if ((!MF.getProperties().hasProperty(MachineFunctionProperties::Property::Divergence))) {
    LLVM_DEBUG(dbgs() << "Function does not have divergence, skip priority "
                         "instructions dup\n");
    return false;
  }

  if (FSAPriDupCount <= 0) 
    return false;

  for (MachineBasicBlock &MBB: MF) {
    for (auto &MI : llvm::make_early_inc_range(MBB)) {
      if (shouldDuplicate(MI)) {
        MadeChange = true;
        for(int Count = 0; Count < FSAPriDupCount; ++Count) {
          auto InsertPos = std::next(MI.getIterator());
          MachineInstr *Dup = MF.CloneMachineInstr(&MI);
          MBB.insert(InsertPos, Dup);
        }
      }
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPriDupPass() {
  return new RISCVFSAPriDup();
}