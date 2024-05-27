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
#include "llvm/Transforms/Utils/LoopUtils.h"

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
    getLoopAnalysisUsage(AU);
  }
};

class InductiveStep : public LoopPass {
 public:
  static char ID;

  InductiveStep() : LoopPass(ID) {}

  bool runOnLoop(Loop *, LPPassManager &) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    getLoopAnalysisUsage(AU);
  }
};

static RegisterPass<InductiveBase> IB(
    "kind-base-case", "Bound loops for base case verification.");
char InductiveBase::ID;

static RegisterPass<InductiveStep> IS(
    "kind-step-case", "Instrument loops for step case verification.");
char InductiveStep::ID;

CallInst *insertVerifierCall(StringRef Name, Type *Result,
                             ArrayRef<Value *> Args, Instruction *I) {
  Function *F = I->getParent()->getParent();
  Module *M = F->getParent();
  LLVMContext &Ctx = F->getContext();

  AttrBuilder B(Ctx);
  B.addAttribute(Attribute::NoUnwind);
  B.addAttribute(Attribute::NoRecurse);
  B.addAttribute(Attribute::OptimizeNone);
  B.addAttribute(Attribute::NoInline);
  AttributeList attrs =
      AttributeList::get(Ctx, AttributeList::FunctionIndex, B);
  SmallVector<Type *> argTypes = {};
  for (Value *arg : Args) {
    argTypes.push_back(arg->getType());
  }
  auto funcType = FunctionType::get(Result, argTypes, false);
  auto callee = M->getOrInsertFunction(Name, funcType, attrs);

  IRBuilder<> IRB(Ctx);
  IRB.SetInsertPoint(I);
  return IRB.CreateCall(callee, Args);
}

// Insert to loop a backedge-taken counter, returns its instruction.
//
// header: %counter = phi [0 %preheader] [%inc %latch]
//   ...
// latch:
//   ...
//   %inc = add %counter 1 br %header
//
// If header == latch, we need to split the BB to separate the phi and the add
// instructions.
PHINode *insertBackedgeTakenCount(Loop *L, DomTreeUpdater *DTU, LoopInfo *LI,
                                  MemorySSAUpdater *MSSAU) {
  BasicBlock *Header = L->getHeader();
  BasicBlock *Preheader = L->getLoopPreheader();
  LLVMContext &Ctx = Header->getContext();
  IRBuilder<> B(Ctx);

  // Create a counter variable at header to track the backedge-taken count.
  B.SetInsertPoint(&Header->front());
  auto *counterType = Type::getInt32Ty(Ctx);
  PHINode *CounterPhi = B.CreatePHI(counterType, 2, L->getName() + ".counter");
  // Do we need to call CounterPhi->setDebugLoc()?
  CounterPhi->addIncoming(ConstantInt::get(counterType, 0), Preheader);

  // If header == latch, split them.
  if (L->getLoopLatch() == Header) {
    splitBlockBefore(Header, Header->getTerminator(), DTU, LI, MSSAU);
  }

  // Increment the counter by 1 at the end of latch.
  BasicBlock *Latch = L->getLoopLatch();
  B.SetInsertPoint(Latch->getTerminator());
  Value *incCounter = B.CreateAdd(CounterPhi, ConstantInt::get(counterType, 1));
  CounterPhi->addIncoming(incCounter, Latch);
  return CounterPhi;
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
  insertVerifierCall("__VERIFIER_assume", Type::getVoidTy(Ctx),
                     {ConstantInt::get(Type::getInt1Ty(Ctx), 0)},
                     TermUnreachable);

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

// Allocate memory for a variable of given type on the stack, call
// __VERIFIER_make_nondet to havoc its value, and load it before I. Returns the
// load instuction (its value undetermined).
LoadInst *createHavocked(Type *type, Instruction *I) {
  Function *F = I->getParent()->getParent();
  Module *M = F->getParent();
  LLVMContext &Ctx = F->getContext();
  IRBuilder<> IRB(Ctx);
  IRB.SetInsertPoint(I);

  auto *sizeType = Type::getInt32Ty(Ctx);
  AllocaInst *allocaInst = IRB.CreateAlloca(type, nullptr);

  const DataLayout *DL = &M->getDataLayout();
  auto size = DL->getTypeAllocSize(type);
  ArrayRef<Value *> args = {allocaInst, ConstantInt::get(sizeType, size),
                            ConstantDataArray::getString(Ctx, "")};

  insertVerifierCall("__VERIFIER_make_nondet", Type::getVoidTy(Ctx), args, I);
  return IRB.CreateLoad(type, allocaInst, "havoc");
}

bool isVerifierAssert(Function *call) {
  return call->getName() == "__VERIFIER_assert";
}

bool InductiveStep::runOnLoop(Loop *L, LPPassManager & /*LPM*/) {
  if (K == 0) return false;

  BasicBlock *Header = L->getHeader();
  BasicBlock *Preheader = L->getLoopPreheader();
  BasicBlock *Latch = L->getLoopLatch();
  Function *F = Header->getParent();
  Module *M = F->getParent();

  // If a phi node in header have both incoming edge from Latch(update) and
  // Preheader (init), havoc the init value.
  Instruction *I = Preheader->getTerminator();
  for (auto &phiNode : Header->phis()) {
    if (phiNode.getBasicBlockIndex(Preheader) != -1 &&
        phiNode.getBasicBlockIndex(Latch) != -1) {
      LoadInst *havocInst = createHavocked(phiNode.getType(), I);
      phiNode.setIncomingValueForBlock(Preheader, havocInst);
    }
  }

  LLVMContext &Ctx = Header->getContext();
  IRBuilder<> B(Ctx);

  // Create a counter variable at header to track the backedge-taken count.
  B.SetInsertPoint(&Header->front());
  auto *counterType = Type::getInt32Ty(Ctx);
  PHINode *CounterPhi = B.CreatePHI(counterType, 2, L->getName() + ".counter");
  // Maybe we also need to call CounterPhi->setDebugLoc()?
  CounterPhi->addIncoming(ConstantInt::get(counterType, 0), Preheader);

  // If counter exceeds K, call assume(0) to inform the verifier and abort
  // immediately. We need to insert the branch before header's terminator,
  // thereby splitting it into two BBs. Caution: this may change Latch if Header
  // == Latch.
  B.SetInsertPoint(Header->getTerminator());
  Value *bound = ConstantInt::get(counterType, K);
  Value *SGTBoundInst = B.CreateICmpSGT(CounterPhi, bound);

  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  Instruction *TermUnreachable = SplitBlockAndInsertIfThen(
      SGTBoundInst, Header->getTerminator(), true, nullptr, &DT, &LI);
  B.SetInsertPoint(TermUnreachable);
  FunctionType *abortFuncType = FunctionType::get(Type::getVoidTy(Ctx), false);
  auto abortFunc = M->getOrInsertFunction("abort", abortFuncType);
  B.CreateCall(abortFunc);

  // replace all calls to __VERIFIER_assert with __VERIFIER_assert_or_assume.
  B.SetInsertPoint(Header->getFirstNonPHI());
  Value *SLTBoundInst = B.CreateICmpSLT(CounterPhi, bound, "flag");
  // FunctionType *verifierAssertType = FunctionType::get(Type::getVoidTy(Ctx),
  // Type::getInt32Ty(Ctx), false); auto callee =
  // M->getOrInsertFunction("__VERIFIER_assert", verifierAssertType);
  SmallVector<CallInst *> callInsts;
  for (auto BB : L->getBlocks()) {
    for (auto &I : *BB) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        Function *calledFunc = CI->getCalledFunction();
        if (calledFunc && isVerifierAssert(calledFunc)) {
          // Replace the call to oldFunc with a call to newFunc
          callInsts.push_back(CI);
        }
      }
    }
  }
  for (CallInst *CI : callInsts) {
    ArrayRef<Value *> args = {CI->getArgOperand(0), SLTBoundInst};
    auto *newCI = insertVerifierCall("__VERIFIER_assert_or_assume",
                                     Type::getVoidTy(Ctx), args, CI);
    CI->replaceAllUsesWith(newCI);
    CI->eraseFromParent();
  }

  // Increment the counter by 1 at the end of latch.
  Latch = L->getLoopLatch();
  B.SetInsertPoint(Latch->getTerminator());
  Value *incCounter = B.CreateAdd(CounterPhi, ConstantInt::get(counterType, 1));
  CounterPhi->addIncoming(incCounter, Latch);

  // All assertions should be changed to assumptions if counter < K. This can be
  // done by an external call to  __VERIFIER_assert_or_assume, with an extra
  // flag argument.

  return true;
}
}  // namespace
