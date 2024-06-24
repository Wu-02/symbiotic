#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::PatternMatch;

/// Returns true if v is used by assume
static bool hasAssumeUsers(Value &v) {
  // for (User *U : v.users())
  //   if (CallInst *ci = dyn_cast<CallInst>(U))
  //     if (match(ci, m_Intrinsic<Intrinsic::assume>())) return true;

  return false;
}

class RenameClamAssume : public FunctionPass {
 public:
  static char ID;

  RenameClamAssume() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    if (F.empty()) return false;

    bool Changed = false;

    LLVMContext &ctx = F.getContext();
    IRBuilder<> Builder(ctx);
    Module *M = F.getParent();
    FunctionType *fty = FunctionType::get(Type::getVoidTy(ctx), Type::getInt1Ty(ctx), false);

    std::vector<Instruction *> to_remove;

    for (auto &I : llvm::make_range(inst_begin(F), inst_end(F))) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        CallBase &CB = *CI;
        const Function *fn = CB.getCalledFunction();
        if (!fn && CB.getCalledOperand()) {
          fn = dyn_cast<const Function>(
              CB.getCalledOperand()->stripPointerCasts());
        }

        if (fn && (fn->getName().equals("verifier.assume") ||
                   fn->getName().equals("verifier.assume.not"))) {
          Changed = true;
          Value *arg = CB.getArgOperand(0);

          // already used in llvm.assume.
          if (hasAssumeUsers(*arg)) {
            to_remove.push_back(CI);
            continue;
          }

          /* insert after verifier.assume, otherwise, verifier assume
           might get simplified away */
          Builder.SetInsertPoint(I.getParent(), ++BasicBlock::iterator(I));
          if (fn->getName().equals("verifier.assume.not")) {
            arg = Builder.CreateNot(arg);
          }

          auto callee = M->getOrInsertFunction("__VERIFIER_assume", fty);
          CallInst *c = Builder.CreateCall(callee, arg);
          
          /*
            mark this assumption so that we know who inserted it
            use c->getMetadata(crallvm) to test.
          */
          c->setMetadata(F.getParent()->getMDKindID("clam"),
                         MDNode::get(ctx, None));

          /*
            enqueue verifier.assume to be removed
          */
          to_remove.push_back(&I);
        }
      }
    }

    while (!to_remove.empty()) {
      Instruction *I = to_remove.back();
      to_remove.pop_back();
      I->eraseFromParent();
    }

    return Changed;
  }
};

char RenameClamAssume::ID;

static llvm::RegisterPass<RenameClamAssume> X(
    "rename-clam-assume", "Rename clam.assume to __VERIFIER_assume.");
