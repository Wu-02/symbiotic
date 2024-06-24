#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"

using namespace llvm;

namespace {

class ExplicitIntLoads : public LoopPass {
 public:
  static char ID;

  ExplicitIntLoads() : LoopPass(ID) {}

  bool runOnLoop(Loop *L, LPPassManager & /*LPM*/) override {
    BasicBlock *Header = L->getHeader();
    LLVMContext &Ctx = Header->getContext();
    IRBuilder<> B(Ctx);
    B.SetInsertPoint(Header->getTerminator());
    bool changed = false;

    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();

    std::set<Value *> ptrs;
    for (auto BB : L->getBlocks()) {
      for (auto &I : *BB) {
        if (auto *storeInst = dyn_cast<StoreInst>(&I)) {
          Value *ptr = storeInst->getPointerOperand();
          ptrs.insert(ptr);
        }
      }
    }
    for (auto ptr : ptrs) {
      if (Instruction *inst = dyn_cast<Instruction>(ptr)) {
        BasicBlock *defBB = inst->getParent();
        if (!DT.dominates(defBB, Header)) {
          continue;
        }
        if (PointerType *PT = dyn_cast<PointerType>(ptr->getType())) {
          Type *ElemType = PT->getElementType();
          if (ElemType->isIntegerTy()) {
            changed |= true;
            B.CreateLoad(ElemType, ptr, ptr->getName() + ".ex");
          }
        }
      }
    }

    return changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    LoopPass::getAnalysisUsage(AU);
  }
};

char ExplicitIntLoads::ID = 0;
static RegisterPass<ExplicitIntLoads> X(
    "explicit-int-loads",
    "Insert explicit load instructions of objects of int type at loop header."
);

};