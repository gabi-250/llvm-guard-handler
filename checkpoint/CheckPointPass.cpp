#include <vector>
#include <llvm/Pass.h>
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
      if (mod.getFunction("llvm.experimental.stackmap") == nullptr) {
        LLVMContext &ctx = mod.getContext();
        Type* i64 = Type::getInt64Ty(ctx);
        Type* i32 = Type::getInt32Ty(ctx);
        std::vector<Type*> types = std::vector<Type*> { i64, i32 };
        FunctionType* signature = FunctionType::get(Type::getVoidTy(ctx),
                                                    types, true);
        Function *stackmap_func = Function::Create(signature,
              Function::ExternalLinkage, "llvm.experimental.stackmap", &mod);
        return true;
      }
      return false;
    }

    virtual bool runOnFunction(Function &fun) {
      errs() << "Function: " <<  fun.getName() << '\n';
      bool modified = false;
      for (auto &bb : fun) {
        for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
          if (isJump(it->getOpcode())) {
            LLVMContext &ctx = bb.getContext();
            Type* i64 = Type::getInt64Ty(ctx);
            Type* i32 = Type::getInt32Ty(ctx);
            Module *mod = fun.getParent();
            IRBuilder<> builder(&bb, it);
            auto args = std::vector<Value*> { ConstantInt::get(i64, sm_id++),
                                              ConstantInt::get(i32, 0) };
            auto sm = builder.CreateCall(
                mod->getFunction("llvm.experimental.stackmap"), args);
            errs() << it->getOpcodeName() <<' ' << it->getOpcode() << '\n';
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
