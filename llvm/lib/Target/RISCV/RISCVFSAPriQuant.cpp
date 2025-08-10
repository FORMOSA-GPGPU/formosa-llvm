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
#include <cmath>
#include <iterator>
#include <unordered_set>
#include <queue>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPriQuant"
extern cl::opt<int> FSAMaxPri;
namespace {
class RISCVFSAPriQuant : public MachineFunctionPass {
private:
// Target Reg info
const RISCVRegisterInfo *TRI;
const RISCVInstrInfo *TII;

public:
static char ID;
RISCVFSAPriQuant() : MachineFunctionPass(ID) {}
void initialize(MachineFunction &F);
bool runOnMachineFunction(MachineFunction &MF) override;
StringRef getPassName() const override {
    return "RISCVFSAPriQuant";
}
};
} // namespace

char RISCVFSAPriQuant::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSAPriQuant, DEBUG_TYPE,
                      "FSA quantize given pri into a smaller int scope ",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPriQuant, DEBUG_TYPE,
                      "FSA quantize given pri into a smaller int scope ",
                     false, false)

void RISCVFSAPriQuant::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

bool RISCVFSAPriQuant::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPriQuant on function: "
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

  std::set<int> FSAPriSet;
  std::unordered_map<int, int> PriRank;
  for (MachineBasicBlock &MBB: MF) {
    for (auto &MI : MBB) {
      auto OpCode = MI.getOpcode();
      if(OpCode == RISCV::FSA_PRI_RAISE_N || OpCode == RISCV::FSA_PRI_LOWER_N || OpCode == RISCV::FSA_PRI_SET) {
        FSAPriSet.insert(MI.getOperand(0).getImm());
      }
    }
  }
  int UniquePriCnt = FSAPriSet.size();
  if (UniquePriCnt == 0)
    return false;

  int BaseRank = 1;
  int MaxPri = -1;
  for(int Pri : FSAPriSet) {
    PriRank[Pri] = BaseRank;
    BaseRank++;
    MaxPri = std::max(Pri, MaxPri);
  }

  if (MaxPri <= FSAMaxPri)
    return false; // No need to quant

  for (MachineBasicBlock &MBB : MF) {
    for (auto &MI : MBB) {
      auto OpCode = MI.getOpcode();
      if(OpCode == RISCV::FSA_PRI_RAISE_N || OpCode == RISCV::FSA_PRI_LOWER_N ||OpCode == RISCV::FSA_PRI_SET) {
        int OriginalPri = MI.getOperand(0).getImm();
        int QuantizedPri = std::ceil(FSAMaxPri * (static_cast<double>(PriRank[OriginalPri]) / UniquePriCnt));
        if (QuantizedPri != OriginalPri) {
          MI.getOperand(0).setImm(QuantizedPri);
          MadeChange = true;
        }
      }
    }
  }
  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPriQuantPass() {
  return new RISCVFSAPriQuant();
}