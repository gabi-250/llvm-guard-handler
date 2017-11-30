#include <vector>
#include <llvm/Pass.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
using namespace llvm;

namespace {

  bool isJump(unsigned);

  struct CheckPointPass: public FunctionPass {
    static char id;
    static int sm_id;

    CheckPointPass() : FunctionPass(id) {}

    virtual bool doInitialization(Module &mod) {
      return false;
    }

    virtual bool runOnFunction(Function &fun) {
      errs() << "Running on function: " <<  fun.getName() << '\n';
      bool modified = false;
      Value * ret = nullptr;
      if (fun.getName() == "f") {
        IRBuilder<> builder(fun.getEntryBlock().getFirstNonPHI());
        ret = builder.CreateCall(Intrinsic::getDeclaration(
            fun.getParent(), Intrinsic::addressofreturnaddress));
        errs() << "Address of return address of function "
               <<  fun.getName() << ": "<< ret << '\n';
        modified = true;
      }
      for (auto &bb : fun) {
        for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
          if (isJump(it->getOpcode())) {
            LLVMContext &ctx = bb.getContext();
            Module *mod = fun.getParent();
            IRBuilder<> builder(&bb, it);
            auto args = std::vector<Value*> { builder.getInt64(sm_id++),
                                              builder.getInt32(0) };
            if (fun.getName() == "f" && ret) {
              args.push_back(ret);
            }
            auto sm = builder.CreateCall(Intrinsic::getDeclaration(
                  fun.getParent(), Intrinsic::experimental_stackmap), args);
            modified = true;
          }
        }
      }
      return modified;
    }

    bool isJump(unsigned opCode) {
      switch (opCode) {
        case Instruction::Br: return true;
        case Instruction::Switch: return true;
        case Instruction::IndirectBr: return true;
        case Instruction::PHI: return true;
        case Instruction::Select: return true;
        default: return false;
      }
    }
  };
}

char CheckPointPass::id = 0;
int CheckPointPass::sm_id = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new CheckPointPass());
}
static RegisterStandardPasses RegisterPass(
    PassManagerBuilder::EP_EarlyAsPossible, registerPass);
