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
      // create exit function
      FunctionType* signature = FunctionType::get(
          Type::getVoidTy(mod.getContext()),
         {Type::getInt32Ty(mod.getContext())}, false);
      Constant *exit_func = mod.getOrInsertFunction("exit",
          signature);
      if (!exit_func) {
        errs() << "exit not in symbol table\n";
        exit(1);
      }

      std::vector<Type*> args(1, Type::getInt32Ty(mod.getContext()));
      FunctionType *fun_type = FunctionType::get(
          Type::getVoidTy(mod.getContext()), args, false);
      Function *fun = Function::Create(fun_type, Function::ExternalLinkage,
                                     "guard_failure", &mod);
      BasicBlock *bb = BasicBlock::Create(mod.getContext(), "entry", fun);

      IRBuilder<> builder(bb);
      builder.SetInsertPoint(bb);

      auto call_inst = builder.CreateCall(
          mod.getFunction("exit"), &*fun->arg_begin());
      builder.CreateRetVoid();
      return true;
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
            // XXX assume it's a conditional br instruction for now
            BranchInst *br_inst = dyn_cast<BranchInst>(it);
            if (br_inst->isConditional()) {
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
              BasicBlock *bb_false = br_inst->getSuccessor(1);
              builder.SetInsertPoint(bb_false, bb_false->begin());
              builder.CreateCall(mod->getFunction("guard_failure"),
                                 builder.getInt32(sm_id - 1));
            }
            modified = true;
          }
        }
      }
      return modified;
    }

    bool isJump(unsigned opCode) {
      switch (opCode) {
        case Instruction::Br: return true;
        //case Instruction::Switch: return true;
        //case Instruction::IndirectBr: return true;
        //case Instruction::PHI: return true;
        //case Instruction::Select: return true;
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
