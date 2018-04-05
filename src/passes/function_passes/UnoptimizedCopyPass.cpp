#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include "Utils.h"

#define UNOPT_PREFIX "__unopt_"
#define GUARD_FUN "__guard_failure"

using namespace llvm;

namespace {

/*
 * Create an unoptimized version of each function defined in this module.
 */
struct UnoptimizedCopyPass: public FunctionPass {
  static char id;

  UnoptimizedCopyPass() : FunctionPass(id) {}

  virtual bool doInitialization(Module &mod) {
    for (auto &fun : mod.functions()) {
      // only duplicate the functions that are defined in this module
      if (!fun.getName().startswith(UNOPT_PREFIX) && !fun.isDeclaration()
          && !fun.hasAvailableExternallyLinkage())  {
        ValueToValueMapTy val;
        Function *unopt_fun = CloneFunction(&fun, val);
        // NoInline is required if OptimizeNone is set
        unopt_fun->addFnAttr(llvm::Attribute::NoInline);
        unopt_fun->addFnAttr(llvm::Attribute::OptimizeNone);
        // the unoptimized clone of each function starts with the
        // '__unopt_prefix'
        unopt_fun->setName(UNOPT_PREFIX + fun.getName().str());
      }
    }
    return true;
  }

  /*
   * Replace calls in __unopt_ functions with calls to other __unopt_ functions.
   */
  virtual bool runOnFunction(Function &fun) {
    auto funName = fun.getName();
    outs() << "Running UnoptCopyPass on function: " << funName << '\n';
    if (funName.startswith(UNOPT_PREFIX)) {
      Module *mod = fun.getParent();
      for (auto &bb : fun) {
        for (auto &inst : bb) {
          if (isa<CallInst>(inst)) {
            // Replace each call in this function with a call to the
            // unoptimized version of the called function.
            CallInst &call = cast<CallInst>(inst);
            if (call.isInlineAsm()) {
              continue;
            }
            Function *calledFun = call.getCalledFunction();
            if (getPatchpointType(calledFun).isPatchpoint()) {
              Value *callback = call.getArgOperand(2)->stripPointerCasts();
              if (callback && isa<Function>(callback)) {
                Function *callbackFunction = cast<Function>(callback);
                StringRef calledFunName = callbackFunction->getName();
                if (calledFunName != GUARD_FUN &&
                    !calledFunName.startswith(UNOPT_PREFIX)) {
                  // This is not an unoptimized function -> call the unoptimized
                  // version of this function instead.
                  Function *newFun =
                    mod->getFunction(UNOPT_PREFIX + calledFunName.str());
                  if (newFun) {
                    Type *i8ptr_t = PointerType::getUnqual(
                        IntegerType::getInt8Ty(mod->getContext()));
                    Constant* newCallback =
                      ConstantExpr::getBitCast(newFun, i8ptr_t);
                    call.setArgOperand(2, newCallback);
                  }
                }
              }
            }
          }
        }
      }
    }
    return true;
  }
};

} // end anonymous namespace

char UnoptimizedCopyPass::id = 0;

static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new UnoptimizedCopyPass());
}
static RegisterStandardPasses RegisterPass(
    PassManagerBuilder::EP_EarlyAsPossible, registerPass);
