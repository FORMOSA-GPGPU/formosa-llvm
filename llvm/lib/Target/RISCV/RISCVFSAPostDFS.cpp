#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "RISCVInstrInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <queue>
using namespace llvm;
#define DEBUG_TYPE "RISCVFSAPostDFS"

namespace {
class RISCVFSAPostDFS : public MachineFunctionPass {
private:
  // Target Reg info
  const RISCVRegisterInfo *TRI;
  const RISCVInstrInfo *TII;
  MachinePostDominatorTree *MPDT;
  MachineLoopInfo *MLI;

public:
  static char ID;
  RISCVFSAPostDFS() : MachineFunctionPass(ID) {}
  int bfsDepth(const MachineBasicBlock* Start, bool FollowSucc,
    const std::unordered_set<MachineBasicBlock *>& ReconvBBSet, 
    const std::unordered_map<const MachineBasicBlock *, int>& DistFromEntry,
    const std::unordered_map<const MachineBasicBlock *, int>& DistFromExit
  );
  std::unordered_map <const MachineBasicBlock*, int> bfsDistFromEntry(const MachineBasicBlock &EntryMBB);
  std::unordered_map <const MachineBasicBlock*, int> bfsDistFromExit(const std::unordered_set<MachineBasicBlock *>& ExitMBBSet);
  
  void initialize(MachineFunction &F);
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "RISCVFSAPostDFS";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char RISCVFSAPostDFS::ID = 0;


INITIALIZE_PASS_BEGIN(RISCVFSAPostDFS, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on post order dfs, use argument "
                      "-fsa-post-dfs to enable the pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(RISCVFSAPostDFS, DEBUG_TYPE,
                      "FSA handling reconv priority by inserting fsa.pri "
                      "instructions based on post order dfs, use argument "
                      "-fsa-post-dfs to enable the pass",
                      false, false)

void RISCVFSAPostDFS::initialize(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
}

  
int RISCVFSAPostDFS::bfsDepth(const MachineBasicBlock* Start, bool FollowSucc,
  const std::unordered_set<MachineBasicBlock *>& ReconvBBSet, 
  const std::unordered_map<const MachineBasicBlock* , int>& DistFromEntry,
  const std::unordered_map<const MachineBasicBlock* , int>& DistFromExit) {
  printf("BFS for BB%d\n", Start->getNumber());
  int MaxDepth = 2;
  std::unordered_map<const MachineBasicBlock*, int> DepthToNearestExit;
  std::unordered_set<const MachineBasicBlock*> BFSVisted;
  std::queue<const MachineBasicBlock*> BFSQueue;
  BFSQueue.push(Start);
  BFSVisted.insert(Start);
  DepthToNearestExit[Start] = 2;
  // Perform BFS, early return if meet end point
  while(!BFSQueue.empty()) {
    const MachineBasicBlock *MBB = BFSQueue.front();
    BFSQueue.pop();
    int Depth = DepthToNearestExit[MBB];
    MaxDepth = std::max(Depth, MaxDepth);
    const auto &FollowMBBs = FollowSucc ? MBB->successors() : MBB->predecessors();
    int CurDistFromEntry = DistFromEntry.at(MBB);
    int CurDistFromExit = DistFromExit.at(MBB);
    for(MachineBasicBlock *FollowMBB : FollowMBBs) {
      int FollowMBBDistFromEntry = DistFromEntry.at(FollowMBB);
      int FollowMBBDistFromExit = DistFromExit.at(FollowMBB);
      printf("CurBB: %d, access BB: %d\n", MBB->getNumber(), FollowMBB->getNumber());
      printf("CurBB from entry = %d, from exit = %d\n", CurDistFromEntry, CurDistFromExit);
      printf("access BB%d from entry = %d, from exit = %d\n", FollowMBB->getNumber(), FollowMBBDistFromEntry, FollowMBBDistFromExit);
      if(!BFSVisted.count(FollowMBB)) {

        // A back edge, skip!!
        if (FollowMBBDistFromEntry < CurDistFromEntry && FollowMBBDistFromExit > CurDistFromExit) {
          BFSVisted.insert(FollowMBB);
          printf("Skip BB%d\n", FollowMBB->getNumber());
          continue;
        }
        if(FollowMBB->pred_size() > 1 && ReconvBBSet.count(FollowMBB)){
          Depth = MaxDepth + 1;
          MaxDepth += 1;
          printf("BB%d's depth become %d by meet BB%d\n", MBB->getNumber(), Depth, FollowMBB->getNumber());
        }
        BFSQueue.push(FollowMBB);
        BFSVisted.insert(FollowMBB);
        DepthToNearestExit[FollowMBB] = Depth;
      }
    }
  }
  printf("\n");
  return MaxDepth;
}

std::unordered_map <const MachineBasicBlock*, int> RISCVFSAPostDFS::bfsDistFromEntry(const MachineBasicBlock &EntryMBB) {
  // BFS from entry to gather dist from entry to a BB
  std::unordered_map<const MachineBasicBlock*, int> BFSDistFromEntry;
  std::unordered_set<const MachineBasicBlock*> BFSVisted;
  std::queue<const MachineBasicBlock*> BFSQueue;
  BFSQueue.push(&EntryMBB);
  BFSVisted.insert(&EntryMBB);
  BFSDistFromEntry[&EntryMBB] = 0;

  while(!BFSQueue.empty()) {
    const MachineBasicBlock *MBB = BFSQueue.front();
    BFSQueue.pop();
    int Depth = BFSDistFromEntry[MBB];
    for(const MachineBasicBlock *Pred : MBB->successors()) {
      if(!BFSVisted.count(Pred)) {
        BFSQueue.push(Pred);
        BFSVisted.insert(Pred);
        BFSDistFromEntry[Pred] = Depth + 1;
      }
    }
  }
  return BFSDistFromEntry;
}

std::unordered_map <const MachineBasicBlock*, int> RISCVFSAPostDFS::bfsDistFromExit(const std::unordered_set<MachineBasicBlock *>& ExitMBBSet) {
  // BFS from entry to gather dist from entry to a BB
  std::unordered_map<const MachineBasicBlock*, int> BFSDistFromExit;
  std::unordered_set<const MachineBasicBlock*> BFSVisted;
  std::queue<const MachineBasicBlock*> BFSQueue;
  for (MachineBasicBlock *MBB : ExitMBBSet) {
    BFSQueue.push(MBB);
    BFSVisted.insert(MBB);
    BFSDistFromExit[MBB] = 0;
  }

  while(!BFSQueue.empty()) {
    const MachineBasicBlock *MBB = BFSQueue.front();
    BFSQueue.pop();
    int Depth = BFSDistFromExit[MBB];
    for(const MachineBasicBlock *Pred : MBB->predecessors()) {
      if(!BFSVisted.count(Pred)) {
        BFSQueue.push(Pred);
        BFSVisted.insert(Pred);
        BFSDistFromExit[Pred] = Depth + 1;
      }
    }
  }
  return BFSDistFromExit;
}

bool RISCVFSAPostDFS::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(
      dbgs() << "------------------------------------------------------------"
                "\n";
      dbgs() << "Running RISCVFSAPostDFS on function: "
             << MF.getName() << "\n";
      dbgs() << "------------------------------------------------------------"
                "\n\n\n";);
  bool MadeChange = false;
  uint64_t MaxReconvPri = 0;
  uint64_t LastPri = 0;
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

  // auto EntryTermIt = EntryMBB.getFirstTerminator();


  std::unordered_set<MachineBasicBlock *> ExitMBBSet;
  std::unordered_set<MachineBasicBlock *> ReconvBBSet;
  std::unordered_set<MachineBasicBlock *> ExcludeBBSet;
  int MergePointCnt = 1;
  bool InsertInExit = false;
  for(MachineBasicBlock &MBB: MF) {
    bool IsExitMBB = MBB.succ_empty();
    if (IsExitMBB){
      ExitMBBSet.insert(&MBB);
      if(MBB.pred_size() > 1)
        InsertInExit = true;
      continue;
    }

    if (MBB.pred_size() > 1) {
      // A SelfLoop reconv edge is meaningless
      bool HasSelfLoop = false;
      for (MachineBasicBlock *PredMBB : MBB.predecessors()) {
        if(PredMBB == &MBB) {
          HasSelfLoop = true;
          break;
        }
      }
      if(MBB.pred_size() == 2 && HasSelfLoop) {
        // Divergence is caused by self loop, skip
        continue;
      }

      if(MachineLoop *Loop = MLI->getLoopFor(&MBB)) {
        MachineBasicBlock *LoopHeaderBB = Loop->getHeader();
        // The headerBB's only reconvgergence edge is a backedge
        // no need to change priority
        if(LoopHeaderBB && LoopHeaderBB->pred_size() == 2) {
          ExcludeBBSet.insert(LoopHeaderBB);
        }
      }
      ReconvBBSet.insert(&MBB);
      MergePointCnt++;
    }
  }
  int StartPri = 1 + InsertInExit;

  if(ReconvBBSet.size()) {
    for(auto *MBB: post_order(&MF.front())) {
      if(ReconvBBSet.count(MBB) && !ExcludeBBSet.count(MBB)) {
        BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
        TII->get(RISCV::FSA_PRI_SET)).addImm(StartPri);
        StartPri++;
        MadeChange = true;
      }
    }
  }

  MachineBasicBlock &EntryMBB = *MF.begin();
  bool InsertInEntry = (EntryMBB.succ_size() > 1 && MadeChange) || (!MadeChange && InsertInExit);
  if (InsertInEntry) {
    MadeChange = true;
    auto EntryTermIt = EntryMBB.getFirstTerminator();
    BuildMI(EntryMBB, EntryTermIt, EntryMBB.findDebugLoc(EntryTermIt),
      TII->get(RISCV::FSA_PRI_SET)).addImm(StartPri);
  }

  if (InsertInExit) {
    for (MachineBasicBlock *MBB : ExitMBBSet) {
      BuildMI(*MBB, MBB->begin(), MBB->findDebugLoc(MBB->begin()),
        TII->get(RISCV::FSA_PRI_SET)).addImm(1);
    }
  }

  // printf("\nPost order traversal:\n");
  // for(auto *MBB: post_order(&MF.front())) {
  //   if(ReconvBBSet.count(MBB))
  //     printf("Access MBB%d\n", MBB->getNumber());
  // }

  // printf("\nDFS traversal:\n");
  // for(auto *MBB: depth_first(&MF.front())) {
  //   printf("Access MBB%d\n", MBB->getNumber());
  // }

  
  std::unordered_map <const MachineBasicBlock*, int> BFSDistFromExit = bfsDistFromExit(ExitMBBSet);

  std::unordered_map <const MachineBasicBlock*, int> BFSDistFromEntry = bfsDistFromEntry(EntryMBB);


  // std::map<MachineBasicBlock *, int> MBBPri;
  // int GlobalMaxPri = 1;
  // for(MachineBasicBlock *ReconvBB : ReconvBBSet) {
  //   int MaxPri = 0;
  //   int ToExitDepth = bfsDepth(ReconvBB, true, ReconvBBSet, BFSDistFromEntry, BFSDistFromExit);
  //   MaxPri = std::max(MaxPri, ToExitDepth);
  //   BuildMI(*ReconvBB, ReconvBB->begin(), ReconvBB->findDebugLoc(ReconvBB->begin()),
  //     TII->get(RISCV::FSA_PRI_SET)).addImm(MaxPri);
  //   MadeChange = true;
  //   // MBBPri[ReconvBB] = MaxPri;
  //   printf("Local max pri: %d\n", MaxPri);
  //   GlobalMaxPri = std::max(MaxPri, GlobalMaxPri);
  // }
  // GlobalMaxPri += 1;
  // auto EntryTermIt = EntryMBB.getFirstTerminator();
  // if(MadeChange)
  //   BuildMI(EntryMBB, EntryTermIt, EntryMBB.findDebugLoc(EntryTermIt),
  //   TII->get(RISCV::FSA_PRI_SET)).addImm(GlobalMaxPri);
  // MaxPri += 1;
  // MachineBasicBlock &EntryMBB = MF.front();

  // for(MachineBasicBlock &MBB : MF) {
  //   auto TermIt = MBB.getFirstTerminator();
  //   if (ExitMBBSet.count(&MBB)){
  //     continue;
  //   }

  //   if(MBB.pred_size() == 0) {
  //     // At enter point, set everyone's pri to max
  //     BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
  //     TII->get(RISCV::FSA_PRI_SET))
  //     .addImm(MaxPri);
  //     MadeChange = true;
  //   } else if (MBB.pred_size() > 1) {
  //     int ReconvPri = DepthToNearestExit[&MBB];
  //     bool HasSelfLoop = false;
  //     for (MachineBasicBlock *PredMBB : MBB.predecessors()) {
  //       if(PredMBB == &MBB) {
  //         HasSelfLoop = true;
  //         break;
  //       }
  //     }

  //     if(MBB.pred_size() == 2 && HasSelfLoop) {
  //       // Divergence is caused by self loop, skip
  //       continue;
  //     }

  //     // At reconv entry, set to relative low priority (cmp to MaxPri)
  //     BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
  //             TII->get(RISCV::FSA_PRI_SET))
  //           .addImm(ReconvPri);

  //     // At reconv exit, set back to highest priority
  //     BuildMI(MBB, TermIt, MBB.findDebugLoc(TermIt),
  //             TII->get(RISCV::FSA_PRI_SET))
  //           .addImm(MaxPri);
  //     MadeChange = true;
  //   }
  // }


  // for (MachineBasicBlock &MBB : MF) {
  //   auto TermIt = MBB.getFirstTerminator();
  //   if(MBB.pred_size() == 0) {
  //     // At enter point, raise everyone's Pri
  //     BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(TermIt), TII->get(RISCV::FSA_PRI_RAISE));
  //     MadeChange = true;
  //   } else if(MBB.succ_size() == 0) {
  //     // At exit point, reset everyone'e Pri by inserting a lower corresponding
  //     // to raise at enter point
  //     BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()), TII->get(RISCV::FSA_PRI_LOWER));
  //     MadeChange = true;
  //   } else if (MBB.pred_size() > 1) {
  //     BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()), TII->get(RISCV::FSA_PRI_LOWER));
  //     BuildMI(MBB, TermIt, MBB.findDebugLoc(TermIt), TII->get(RISCV::FSA_PRI_RAISE));
  //     MadeChange = true;
  //   }
  //   // BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
  //           // TII->get(RISCV::FSA_PRI_SET))
  //       // .addImm(FinalLevel);
  // }

  return MadeChange;
}

FunctionPass *llvm::createRISCVFSAPostDFSPass() {
  return new RISCVFSAPostDFS();
}