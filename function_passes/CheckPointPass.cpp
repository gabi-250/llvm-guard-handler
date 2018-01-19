#include <vector>
#include <map>
#include <stdint.h>
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

using namespace llvm;

namespace {

  bool isJump(unsigned);

  struct CheckPointPass: public FunctionPass {
    static char id;
    static uint64_t sm_id;
    static std::map<StringRef, uint64_t> start_sm_id;

    CheckPointPass() : FunctionPass(id) {}

    virtual bool runOnFunction(Function &fun) {
      errs() << "Running CheckPointPass on function: " << fun.getName() << '\n';
      StringRef fun_name = fun.getName();
      if (!fun_name.endswith("trace")) {
        return false;
      }

      StringRef complementary_fun = "";
      if (fun_name.startswith("__unopt_")) {
        complementary_fun = fun_name.substr(8);
      } else {
        complementary_fun = StringRef("__unopt_" + fun_name.str());
      }

      uint64_t curr_sm_id = sm_id;
      if (start_sm_id.find(complementary_fun) != start_sm_id.end()) {
        curr_sm_id = ~start_sm_id[complementary_fun];
      }

      start_sm_id[fun_name] = sm_id;
      uint64_t call_inst_count = 0;
      for (auto &bb : fun) {
        for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
          if (isa<CallInst>(it) &&
              cast<CallInst>(*it).getCalledFunction()->getName() == "putchar") {
            LLVMContext &ctx = bb.getContext();
            Module *mod = fun.getParent();
            IRBuilder<> builder(&bb, it);
            Type *i8ptr_t = PointerType::getUnqual(IntegerType::getInt8Ty(ctx));
            Constant* gf_handler_ptr = ConstantExpr::getBitCast(
                mod->getFunction("__guard_failure"), i8ptr_t);

            uint64_t patchpoint_id;

            if (start_sm_id.find(complementary_fun) == start_sm_id.end()) {
              patchpoint_id = curr_sm_id + call_inst_count;
            } else {
              patchpoint_id = ~(~curr_sm_id + call_inst_count);
            }
            auto args = std::vector<Value*> { builder.getInt64(patchpoint_id),
                                              builder.getInt32(13) // XXX why?
                                            };
            Function *intrinsic = nullptr;
            if (fun_name == "trace") {
              // insert a patchpoint, not a stack map - need extra arguments
              args.insert(args.end(), { gf_handler_ptr,
                                        builder.getInt32(1),
                                        builder.getInt64(patchpoint_id) });
              intrinsic = Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_patchpoint_void);
            } else {
              // unoptimized function
              intrinsic = Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_stackmap);
            }
            for (BasicBlock::iterator prev_inst = bb.begin(); prev_inst != it;
                 ++prev_inst) {
              // XXX need to accurately identify the live registers
              if (prev_inst != it &&
                    prev_inst->use_begin() != prev_inst->use_end()) {
                args.push_back(&*prev_inst);
              }
            }
            auto call_inst = builder.CreateCall(intrinsic, args);
            ++call_inst_count;
          }
        }
      }

      if (start_sm_id.find(complementary_fun) == start_sm_id.end()) {
        sm_id = start_sm_id[fun_name] + call_inst_count;
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
uint64_t CheckPointPass::sm_id = 0;
std::map<StringRef, uint64_t> CheckPointPass::start_sm_id;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new CheckPointPass());
}
static RegisterStandardPasses RegisterPass(
    PassManagerBuilder::EP_EarlyAsPossible, registerPass);
