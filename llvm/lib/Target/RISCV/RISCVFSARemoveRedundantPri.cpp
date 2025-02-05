#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"

using namespace llvm;
#define DEBUG_TYPE "RISCVFSARemoveRedundantPri"

namespace {
class RISCVFSARemoveRedundantPri : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;

public:
  static char ID;
  RISCVFSARemoveRedundantPri() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSARemoveRedundantPri";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSARemoveRedundantPri::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSARemoveRedundantPri, DEBUG_TYPE,
                      "FSA remove redundant priority instructions", false,
                      false)
INITIALIZE_PASS_END(RISCVFSARemoveRedundantPri, DEBUG_TYPE,
                    "FSA remove redundant priority instructions", false, false)

void RISCVFSARemoveRedundantPri::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSARemoveRedundantPri::runOnMachineFunction(MachineFunction &MF) {
  initialize(MF);

  bool HasChanged = false;
  SmallVector<MachineInstr *> WorkList;
  if (!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Divergence)) {
    LLVM_DEBUG(dbgs() << "Function does not have divergence, remove redundant "
                         "priority instructions\n");
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        unsigned Opcode = MI.getOpcode();
        if (Opcode >= RISCV::FSA_PRI_LOWER && Opcode <= RISCV::FSA_PRI_SET) {
          WorkList.push_back(&MI);
        }
      }
    }
  } else {
    // Non-divergent function, remove unecessary priority instructions
    for (auto &MBB : MF) {
      for (auto &MI : MBB) {
        unsigned Opcode = MI.getOpcode();
        if (Opcode == RISCV::FSA_PRI_RAISE) {
          // Check if the next instruction is a FSA_PRI_LOWER
          MachineBasicBlock::iterator NextMI = std::next(MI.getIterator());
          if (NextMI != MBB.end() &&
              NextMI->getOpcode() == RISCV::FSA_PRI_LOWER) {
            WorkList.push_back(&MI);
            WorkList.push_back(&*NextMI);
          }
        }

        if (Opcode == RISCV::FSA_PRI_RAISE_F) {
          // Check if the next instruction is a FSA_PRI_LOWER_F
          MachineBasicBlock::iterator NextMI = std::next(MI.getIterator());
          if (NextMI != MBB.end() &&
              NextMI->getOpcode() == RISCV::FSA_PRI_LOWER_F) {
            WorkList.push_back(&MI);
            WorkList.push_back(&*NextMI);
          }
        }
      }
    }
  }

  while (!WorkList.empty()) {
    MachineInstr *MI = WorkList.pop_back_val();
    MI->eraseFromParent();
    HasChanged = true;
  }

  return HasChanged;
}

FunctionPass *llvm::createRISCVFSARemoveRedundantPriPass() {
  return new RISCVFSARemoveRedundantPri();
}
