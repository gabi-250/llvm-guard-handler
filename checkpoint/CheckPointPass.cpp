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
#include <string>

using namespace llvm;

namespace {

  bool isJump(unsigned);

  struct CheckPointPass: public FunctionPass {
    static char id;
    static long long int sm_id;

    CheckPointPass() : FunctionPass(id) {}

    virtual bool runOnFunction(Function &fun) {
      errs() << "Running on function: " <<  fun.getName() << '\n';
      std::string fun_name = fun.getName();
      if (fun_name != "trace" && fun_name != "unopt") {
        errs() << "Done\n";
        return false;
      }
      for (auto &bb : fun) {
        for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
          // add a patchpoint before each call instruction for now
          if (it->getOpcode() == Instruction::Call) {
            LLVMContext &ctx = bb.getContext();
            Module *mod = fun.getParent();
            IRBuilder<> builder(&bb, it);
            Type *i8ptr_t = PointerType::getUnqual(IntegerType::getInt8Ty(ctx));
            Constant* gf_handler_ptr = ConstantExpr::getBitCast(
                mod->getFunction("__guard_failure"), i8ptr_t);
            auto args = std::vector<Value*> { builder.getInt64(sm_id++),
                                              builder.getInt32(13) // XXX why?
                                            };
            Function *intrinsic = nullptr;
            if (fun_name == "trace") {
              // insert a patchpoint, not a stack map - need extra arguments
              args.insert(args.end(), { gf_handler_ptr,
                                        builder.getInt32(1),
                                        builder.getInt64(sm_id - 1) });
              intrinsic = Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_patchpoint_void);
            } else if (fun_name == "unopt") {
              intrinsic = Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_stackmap);
            }
            for (BasicBlock::iterator prev_inst = bb.begin(); prev_inst != bb.end();
                 ++prev_inst) {
              // XXX need to accurately identify the live registers
                errs() << "Recording " <<  *prev_inst << "\n";
              if (prev_inst != it &&
                    prev_inst->use_begin() != prev_inst->use_end()) {
                errs() << "Recording " <<  *prev_inst << "\n";
                args.push_back(&*prev_inst);
              }
            }
            auto call_inst = builder.CreateCall(intrinsic, args);
          }
        }
      }
      return true;
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
