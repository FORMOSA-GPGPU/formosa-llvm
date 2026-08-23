#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "RISCVFSACleanUp"

namespace {

class RISCVFSACleanUp : public MachineFunctionPass {
private:
  struct PendingRaise {
    MachineInstr *MI;
    int64_t Amount;
  };

  const RISCVRegisterInfo *TRI = nullptr;
  const RISCVInstrInfo *TII = nullptr;

  static bool isUnitRaise(unsigned Opcode) {
    return Opcode == RISCV::FSA_PRI_RAISE;
  }

  static bool isUnitLower(unsigned Opcode) {
    return Opcode == RISCV::FSA_PRI_LOWER;
  }

  static bool isRaise(unsigned Opcode) {
    return Opcode == RISCV::FSA_PRI_RAISE || Opcode == RISCV::FSA_PRI_RAISE_N;
  }

  static bool isLower(unsigned Opcode) {
    return Opcode == RISCV::FSA_PRI_LOWER || Opcode == RISCV::FSA_PRI_LOWER_N;
  }

  static bool isCanonicalRaise(const MachineInstr &MI) {
    return MI.getOpcode() == RISCV::FSA_PRI_RAISE_N;
  }

  static bool isCanonicalLower(const MachineInstr &MI) {
    return MI.getOpcode() == RISCV::FSA_PRI_LOWER_N;
  }

  static bool isSchedulingFence(const MachineInstr &MI) {
    return MI.getOpcode() == RISCV::FSA_BAR || MI.isTerminator();
  }

  static int64_t getPriAmount(const MachineInstr &MI) {
    if (MI.getOpcode() == RISCV::FSA_PRI_RAISE ||
        MI.getOpcode() == RISCV::FSA_PRI_LOWER)
      return 1;
    return MI.getOperand(0).getImm();
  }

  static void setPriAmount(MachineInstr &MI, int64_t Amount) {
    MI.getOperand(0).setImm(Amount);
  }

  MachineInstr *insertCanonicalPri(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator InsertIt,
                                   unsigned Opcode, const DebugLoc &DL,
                                   int64_t Amount) const {
    return BuildMI(MBB, InsertIt, DL, TII->get(Opcode)).addImm(Amount);
  }

  bool canonicalizeMBB(MachineBasicBlock &MBB);
  bool simplifyMBBOnce(MachineBasicBlock &MBB);
  bool simplifyMBBToFixedPoint(MachineBasicBlock &MBB);

public:
  static char ID;

  RISCVFSACleanUp() : MachineFunctionPass(ID) {}

  void initialize(MachineFunction &MF);
  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "RISCVFSACleanUp"; }
};

} // namespace

char RISCVFSACleanUp::ID = 0;

INITIALIZE_PASS_BEGIN(
    RISCVFSACleanUp, DEBUG_TYPE,
    "FSA handling raise/lower priority by moving and remove fsa.pri ", false,
    false)
INITIALIZE_PASS_END(
    RISCVFSACleanUp, DEBUG_TYPE,
    "FSA handling raise/lower priority by moving and remove fsa.pri ", false,
    false)

void RISCVFSACleanUp::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  (void)TRI;
}

bool RISCVFSACleanUp::canonicalizeMBB(MachineBasicBlock &MBB) {
  bool Changed = false;

  for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
    const unsigned Opcode = MI.getOpcode();

    if (Opcode == RISCV::FSA_RECONV_MARKER) {
      MI.eraseFromParent();
      Changed = true;
      continue;
    }

    if (!isUnitRaise(Opcode) && !isUnitLower(Opcode))
      continue;

    const unsigned CanonicalOpcode =
        isUnitRaise(Opcode) ? RISCV::FSA_PRI_RAISE_N : RISCV::FSA_PRI_LOWER_N;
    insertCanonicalPri(MBB, MI.getIterator(), CanonicalOpcode, MI.getDebugLoc(),
                       1);
    MI.eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool RISCVFSACleanUp::simplifyMBBOnce(MachineBasicBlock &MBB) {
  bool Changed = false;
  SmallVector<PendingRaise, 8> RaiseStack;
  MachineInstr *LastLowerMI = nullptr;

  for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
    // Do not simplify across instructions that may introduce an observable
    // scheduling boundary. Priority effects before the fence must remain
    // independent from those after it.
    if (isSchedulingFence(MI)) {
      RaiseStack.clear();
      LastLowerMI = nullptr;
      continue;
    }

    if (isCanonicalRaise(MI)) {
      const int64_t RaiseAmount = getPriAmount(MI);
      LastLowerMI = nullptr;

      if (!RaiseStack.empty()) {
        PendingRaise &Top = RaiseStack.back();
        Top.Amount += RaiseAmount;
        setPriAmount(*Top.MI, Top.Amount);
        MI.eraseFromParent();
        Changed = true;
        continue;
      }

      RaiseStack.push_back({&MI, RaiseAmount});
      continue;
    }

    if (!isCanonicalLower(MI))
      continue;

    int64_t RemainingLower = getPriAmount(MI);
    bool LowerChanged = false;

    while (RemainingLower > 0 && !RaiseStack.empty()) {
      PendingRaise Top = RaiseStack.pop_back_val();

      if (RemainingLower < Top.Amount) {
        Top.Amount -= RemainingLower;
        setPriAmount(*Top.MI, Top.Amount);
        RaiseStack.push_back(Top);
        MI.eraseFromParent();
        Changed = true;
        LowerChanged = true;
        RemainingLower = 0;
        break;
      }

      if (RemainingLower == Top.Amount) {
        Top.MI->eraseFromParent();
        MI.eraseFromParent();
        Changed = true;
        LowerChanged = true;
        RemainingLower = 0;
        break;
      }

      RemainingLower -= Top.Amount;
      Top.MI->eraseFromParent();
      Changed = true;
      LowerChanged = true;
    }

    if (RemainingLower == 0)
      continue;

    if (LowerChanged) {
      setPriAmount(MI, RemainingLower);
      Changed = true;
    }

    if (RaiseStack.empty()) {
      if (LastLowerMI && LastLowerMI != &MI) {
        const int64_t MergedLower =
            getPriAmount(*LastLowerMI) + getPriAmount(MI);
        setPriAmount(*LastLowerMI, MergedLower);
        MI.eraseFromParent();
        Changed = true;
        continue;
      }

      LastLowerMI = &MI;
      continue;
    }

    LastLowerMI = nullptr;
  }

  return Changed;
}

bool RISCVFSACleanUp::simplifyMBBToFixedPoint(MachineBasicBlock &MBB) {
  bool Changed = false;
  bool LocalChanged = false;

  do {
    LocalChanged = simplifyMBBOnce(MBB);
    Changed |= LocalChanged;
  } while (LocalChanged);

  return Changed;
}

bool RISCVFSACleanUp::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "------------------------------------------------------------\n";
             dbgs() << "Running RISCVFSACleanUp on function: " << MF.getName()
                    << "\n";
             dbgs() << "------------------------------------------------------------\n\n";);

  initialize(MF);

  const bool AllowSkip =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None;
  if (!MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Divergence) &&
      AllowSkip) {
    LLVM_DEBUG(dbgs() << "Function does not have divergence, skip priority "
                         "instructions cleanup\n";);
    return false;
  }

  bool MadeChange = false;
  for (MachineBasicBlock &MBB : MF) {
    MadeChange |= canonicalizeMBB(MBB);
    MadeChange |= simplifyMBBToFixedPoint(MBB);
  }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSACleanUpPass() {
  return new RISCVFSACleanUp();
}
