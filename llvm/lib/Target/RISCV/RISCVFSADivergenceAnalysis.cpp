#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineUniformityAnalysis.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

using namespace llvm;
#define DEBUG_TYPE "RISCVFSADivergenceAnalysis"

namespace {
class RISCVFSADivergenceAnalysis : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  const MachineUniformityInfo *MUA;

public:
  static char ID;
  RISCVFSADivergenceAnalysis() : MachineFunctionPass(ID) {}
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSADivergenceAnalysis";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineUniformityAnalysisPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSADivergenceAnalysis::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVFSADivergenceAnalysis, DEBUG_TYPE,
                      "FSA modify machine function divergence property", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineUniformityAnalysisPass)
INITIALIZE_PASS_END(RISCVFSADivergenceAnalysis, DEBUG_TYPE,
                    "FSA modify machine function divergence property", false,
                    false)

void RISCVFSADivergenceAnalysis::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  MUA = &getAnalysis<MachineUniformityAnalysisPass>().getUniformityInfo();
}

bool RISCVFSADivergenceAnalysis::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  // skip the pass if there is no XFormosaPri extension
  if (!ST.hasFeature(RISCV::FeatureVendorXFormosaPri))
    return false;
  initialize(MF);

  // Set the divergence property if the function has divergence or has argument
  if (MF.getFunction().arg_size() > 0) {
    LLVM_DEBUG(
        dbgs() << "Function has argument, setting MF Divegence property\n");
    MF.getProperties().set(MachineFunctionProperties::Property::Divergence);
  }

  if (MUA->hasDivergence()) {
    LLVM_DEBUG(
        dbgs() << "Function has divergence, setting MF Divegence property\n");
    MF.getProperties().set(MachineFunctionProperties::Property::Divergence);
  }

  return false;
}

FunctionPass *llvm::createRISCVFSADivergenceAnalysisPass() {
  return new RISCVFSADivergenceAnalysis();
}
