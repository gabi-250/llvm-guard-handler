#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#define UNOPT_PREFIX "__unopt_"

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
    if (!fun.getName().startswith(UNOPT_PREFIX)) {
      return false;
    }
    outs() << "Running UnoptCopyPass on function: " << fun.getName() << '\n';
    Module *mod = fun.getParent();
    for (auto &bb : fun) {
      for (auto &inst : bb) {
        if (isa<CallInst>(inst)) {
          // replace all calls inside this unoptimized function with calls to
          // the unoptimized versions of the called functions
          CallInst &call = cast<CallInst>(inst);
          Function *called_fun = call.getCalledFunction();
          if (called_fun) {
            StringRef called_fun_name = called_fun->getName();
            if (!called_fun_name.startswith(UNOPT_PREFIX)) {
              // not an unoptimized function -> must call the unoptimized
              // version of the function instead
              Function *new_fun =
                mod->getFunction(UNOPT_PREFIX + called_fun_name.str());
              if (new_fun) {
                call.setCalledFunction(new_fun);
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
