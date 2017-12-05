#include <vector>
#include <llvm/Pass.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <cstdio>
using namespace llvm;

namespace {

  bool isJump(unsigned);

  struct CheckPointPass: public FunctionPass {
    static char id;
    static long long int sm_id;

    CheckPointPass() : FunctionPass(id) {}

    virtual bool doInitialization(Module &mod) {
      if (mod.getFunction("__guard_failure") == nullptr) {
        LLVMContext &ctx = mod.getContext();
        Type* void_t = Type::getVoidTy(ctx);
        Type* i64_t = Type::getInt64Ty(ctx);
        FunctionType* signature = FunctionType::get(void_t, i64_t, false);
        Function *fun = Function::Create(signature,
                                         Function::ExternalLinkage,
                                         "__guard_failure",
                                         &mod);
        return true;
      } else {
        errs() << "Found \'__guard_failure\' function. Exiting.\n";
        exit(1);
      }
    }

    virtual bool runOnFunction(Function &fun) {
      errs() << "Running on function: " <<  fun.getName() << '\n';
      if (fun.getName() != "trace") {
        return false;
      }
      bool modified = false;
      for (auto &bb : fun) {
        for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
          //if (isJump(it->getOpcode())
          if (it->getOpcode() == Instruction::Call) {
            // add a patchpoint before each call instruction for now
            LLVMContext &ctx = bb.getContext();
            Module *mod = fun.getParent();
            IRBuilder<> builder(&bb, it);

            Type *i8ptr_t = PointerType::getUnqual(IntegerType::getInt8Ty(ctx));
            Constant* gf_handler_ptr = ConstantExpr::getBitCast(
                mod->getFunction("__guard_failure"), i8ptr_t);

            auto args = std::vector<Value*> { builder.getInt64(sm_id++),
                                              builder.getInt32(13), // XXX why?
                                              gf_handler_ptr,
                                              builder.getInt32(1),
                                              builder.getInt64(sm_id - 1) };

            auto call_inst = builder.CreateCall(Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_patchpoint_void), args);
            modified = true;
          }
        }
      }
      return modified;
    }

    bool isJump(unsigned opcode) {
      switch (opcode) {
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
long long int CheckPointPass::sm_id = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new CheckPointPass());
}
static RegisterStandardPasses RegisterPass(
    PassManagerBuilder::EP_EarlyAsPossible, registerPass);
