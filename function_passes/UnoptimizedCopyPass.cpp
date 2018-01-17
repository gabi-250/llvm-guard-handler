#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

using namespace llvm;

namespace {

  struct UnoptimizedCopyPass: public FunctionPass {
    static char id;

    UnoptimizedCopyPass() : FunctionPass(id) {}

    virtual bool doInitialization(Module &mod) {
      for (auto &fun : mod.functions()) {
        if (!fun.getName().startswith("__unopt_") && !fun.isDeclaration()) {
          ValueToValueMapTy val;
          Function *unopt_fun = CloneFunction(&fun, val);
          unopt_fun->setName("__unopt_" + fun.getName().str());
        }
      }
      return true;
    }

    virtual bool runOnFunction(Function &fun) {
      errs() << "Running UnoptCopyPass on function: " << fun.getName() << '\n';
      if (fun.getName().startswith("__unopt_")) {
        fun.addFnAttr(llvm::Attribute::NoInline);
        fun.addFnAttr(llvm::Attribute::OptimizeNone);
        Module *mod = fun.getParent();
        for (auto &bb : fun) {
          for (auto &inst : bb) {
            if (isa<CallInst>(inst)) {
              CallInst &call = cast<CallInst>(inst);
              Function *called_fun = call.getCalledFunction();
              if (called_fun) {
                StringRef called_fun_name = called_fun->getName();
                if (!called_fun_name.startswith("__unopt_")) {
                  Function *new_fun =
                    mod->getFunction("__unopt_" + called_fun_name.str());
                  if (new_fun) {
                    call.setCalledFunction(new_fun);
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
}

char UnoptimizedCopyPass::id = 0;

static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new UnoptimizedCopyPass());
}
static RegisterStandardPasses RegisterPass(
    PassManagerBuilder::EP_EarlyAsPossible, registerPass);
