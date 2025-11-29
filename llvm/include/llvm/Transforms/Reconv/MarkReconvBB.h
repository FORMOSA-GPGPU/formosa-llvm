#pragma once
#include "llvm/IR/PassManager.h"

namespace llvm {
struct MarkReconvBBPass : public PassInfoMixin<MarkReconvBBPass> {
  PreservedAnalyses run(Function &, FunctionAnalysisManager &);
};
} // namespace llvm
