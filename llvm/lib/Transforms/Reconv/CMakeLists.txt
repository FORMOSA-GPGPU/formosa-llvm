add_llvm_component_library(LLVMReconv
  MarkReconvBB.cpp

  LINK_COMPONENTS
    Core
    Analysis         # 用到 Uniformity / PostDominator
    TransformUtils
    Support
)
