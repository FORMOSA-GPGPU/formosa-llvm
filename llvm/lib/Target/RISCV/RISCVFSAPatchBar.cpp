#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "RISCVFSAPatchBar"

namespace {

class RISCVFSAPatchBar : public MachineFunctionPass {
private:
  const RISCVInstrInfo *TII = nullptr;

  static bool hasFsaBar(const MachineFunction &MF) {
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        if (MI.getOpcode() == RISCV::FSA_BAR)
          return true;
      }
    }
    return false;
  }

  static bool hasPriorityInstr(const MachineFunction &MF) {
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        switch (MI.getOpcode()) {
        case RISCV::FSA_PRI_RAISE:
        case RISCV::FSA_PRI_RAISE_N:
        case RISCV::FSA_PRI_LOWER:
        case RISCV::FSA_PRI_LOWER_N:
        case RISCV::FSA_PRI_SET:
          return true;
        default:
          break;
        }
      }
    }
    return false;
  }

  static bool blockHasFsaBar(const MachineBasicBlock &MBB) {
    for (const MachineInstr &MI : MBB) {
      if (MI.getOpcode() == RISCV::FSA_BAR)
        return true;
    }
    return false;
  }

  static MachineBasicBlock::iterator
  blockBeginInsertPt(MachineBasicBlock &MBB) {
    return MBB.SkipPHIsLabelsAndDebug(MBB.begin());
  }

  void insertLower(MachineBasicBlock &MBB, int64_t Amount) const {
    auto InsertPt = blockBeginInsertPt(MBB);
    BuildMI(MBB, InsertPt, MBB.findDebugLoc(InsertPt),
            TII->get(RISCV::FSA_PRI_LOWER_N))
        .addImm(Amount);
  }

  void insertLowerRaisePair(MachineBasicBlock &MBB, int64_t Amount) const {
    auto InsertPt = blockBeginInsertPt(MBB);
    MachineInstr *Lower =
        BuildMI(MBB, InsertPt, MBB.findDebugLoc(InsertPt),
                TII->get(RISCV::FSA_PRI_LOWER_N))
            .addImm(Amount);
    auto RaisePt = std::next(Lower->getIterator());
    BuildMI(MBB, RaisePt, MBB.findDebugLoc(RaisePt),
            TII->get(RISCV::FSA_PRI_RAISE_N))
        .addImm(Amount);
  }

public:
  static char ID;

  RISCVFSAPatchBar() : MachineFunctionPass(ID) {}

  void initialize(MachineFunction &MF) {
    const auto &ST = MF.getSubtarget<RISCVSubtarget>();
    TII = ST.getInstrInfo();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "RISCVFSAPatchBar"; }
};

} // namespace

char RISCVFSAPatchBar::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAPatchBar, DEBUG_TYPE,
                      "Patch FSA_BAR with minimal priority scaffolding", false,
                      false)
INITIALIZE_PASS_END(RISCVFSAPatchBar, DEBUG_TYPE,
                    "Patch FSA_BAR with minimal priority scaffolding", false,
                    false)

bool RISCVFSAPatchBar::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "Running RISCVFSAPatchBar on function: " << MF.getName()
                    << "\n";);
  initialize(MF);

  const bool HasDivergence = MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::Divergence);
  if (!HasDivergence)
    return false;

  if (!hasFsaBar(MF) || hasPriorityInstr(MF))
    return false;

  MachineBasicBlock &EntryMBB = MF.front();
  auto EntryInsertPt = blockBeginInsertPt(EntryMBB);
  BuildMI(EntryMBB, EntryInsertPt, EntryMBB.findDebugLoc(EntryInsertPt),
          TII->get(RISCV::FSA_PRI_RAISE_N))
      .addImm(1);

  for (MachineBasicBlock &MBB : MF) {
    const bool IsExit = MBB.succ_empty();
    const bool HasBar = blockHasFsaBar(MBB);

    if (IsExit) {
      insertLower(MBB, 1);
      continue;
    }

    if (HasBar)
      insertLowerRaisePair(MBB, 1);
  }

  return true;
}

FunctionPass *llvm::createRISCVFSAPatchBarPass() {
  return new RISCVFSAPatchBar();
}
