#include <map>

#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

static cl::opt<unsigned> MaxBackedgeCount(
    "kind-max-backedge-count",
    cl::desc("Max number of loop iterations(backedges taken)"),
    cl::value_desc("N"));

static cl::opt<unsigned> K("kind-k", cl::desc("Parameter k for k-induction."),
                           cl::value_desc("N"));

namespace {
class InductiveBase : public LoopPass {
 public:
  static char ID;

  InductiveBase() : LoopPass(ID) {}

  bool runOnLoop(Loop *, LPPassManager &) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(LoopSimplifyID);
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
  }
};

class InductiveStep : public LoopPass {
 public:
  static char ID;

  InductiveStep() : LoopPass(ID) {}

  bool runOnLoop(Loop *, LPPassManager &) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(LoopSimplifyID);
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
  }
};
}  // namespace

static RegisterPass<InductiveBase> IB(
    "kind-base-case", "Bound loops for base case verification.");
char InductiveBase::ID;

static RegisterPass<InductiveStep> IS(
    "kind-step-case", "Instrument loops for step case verification.");
char InductiveStep::ID;

void insertAssumption(Value *Arg, Instruction *I) {
  Function *F = I->getParent()->getParent();
  Module *M = F->getParent();
  LLVMContext &Ctx = F->getContext();
  IRBuilder<> IRB(Ctx);
  IRB.SetInsertPoint(I);

  AttrBuilder B(Ctx);
  B.addAttribute(Attribute::NoUnwind);
  B.addAttribute(Attribute::NoRecurse);
  B.addAttribute(Attribute::OptimizeNone);
  B.addAttribute(Attribute::NoInline);
  // LLVM removed all calls to __VERIFIER_assume if marked as ReadNone
  // or ReadOnly even if we mark it as OptimizeNone.
  B.addAttribute(Attribute::InaccessibleMemOnly);
  AttributeList as = AttributeList::get(Ctx, AttributeList::FunctionIndex, B);

  auto AssumeFunc = M->getOrInsertFunction(
      "__VERIFIER_assume", as, Type::getVoidTy(Ctx), Type::getInt1Ty(Ctx));
  IRB.CreateCall(AssumeFunc, Arg);
}

bool InductiveBase::runOnLoop(Loop *L, LPPassManager & /*LPM*/) {
  if (MaxBackedgeCount == 0) return false;

  BasicBlock *Header = L->getHeader();
  BasicBlock *Preheader = L->getLoopPreheader();
  Function *F = Header->getParent();
  Module *M = F->getParent();

  LLVMContext &Ctx = Header->getContext();
  IRBuilder<> B(Ctx);

  // Create a counter variable at header to track the backedge-taken count.
  B.SetInsertPoint(&Header->front());
  auto *counterType = Type::getInt32Ty(Ctx);
  PHINode *CounterPhi = B.CreatePHI(counterType, 2, L->getName() + ".counter");
  // Maybe we also need to call CounterPhi->setDebugLoc()?
  CounterPhi->addIncoming(ConstantInt::get(counterType, 0), Preheader);

  // If counter exceeds the bound, call assume(0) to inform the verifier and
  // abort immediately. We need to insert the branch before header's terminator,
  // thereby splitting it into two BBs.
  // Caution: this may change Latch if Header == Latch.
  B.SetInsertPoint(Header->getTerminator());
  Value *bound = ConstantInt::get(counterType, MaxBackedgeCount);
  Value *CmpInst = B.CreateICmpSGT(CounterPhi, bound);

  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  Instruction *TermUnreachable = SplitBlockAndInsertIfThen(
      CmpInst, Header->getTerminator(), true, nullptr, &DT, &LI);
  insertAssumption(ConstantInt::get(Type::getInt1Ty(Ctx), 0), TermUnreachable);

  B.SetInsertPoint(TermUnreachable);
  FunctionType *abortFuncType = FunctionType::get(Type::getVoidTy(Ctx), false);
  auto abortFunc = M->getOrInsertFunction("abort", abortFuncType);
  B.CreateCall(abortFunc);

  // Increment the counter by 1 at the end of latch.
  BasicBlock *Latch = L->getLoopLatch();
  B.SetInsertPoint(Latch->getTerminator());
  Value *incCounter = B.CreateAdd(CounterPhi, ConstantInt::get(counterType, 1));
  CounterPhi->addIncoming(incCounter, Latch);

  return true;
}

bool InductiveStep::runOnLoop(Loop *L, LPPassManager & /*LPM*/) {
  if (K == 0) return false;

  return false;
}
