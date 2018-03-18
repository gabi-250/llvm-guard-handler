#include <vector>
#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include "Utils.h"

using namespace llvm;
using std::vector;

namespace {

/*
 * Insert empty `asm` blocks before and after each callsite marked by a
 * `stackmap` call.
 *
 * An empty `asm` block is inserted before each function call, and after each
 * `stackmap` call to prevent global values from being loaded at an
 * inconvenient point.
 *
 * tail call void asm sideeffect "", ""() #4
 * %16 = tail call i32 @get_number(i32 %14)
 * call void (i64, i32, ...) @llvm.experimental.stackmap(i64 3, i32 13, ...)
 * tail call void asm sideeffect "", ""() #4
 *
 */
struct BarrierPass: public BasicBlockPass {
  static char id;

  BarrierPass() : BasicBlockPass(id) {}
  virtual bool runOnBasicBlock(BasicBlock &bb) {
    auto funName = bb.getParent()->getName();
    outs() << "Running BarrierPass on function: " << funName << '\n';
    vector <Instruction *> callInsts;
    for (auto &inst : bb) {
      if (isa<CallInst>(inst)) {
        CallInst &call = cast<CallInst>(inst);
        if (!call.isInlineAsm()) {
          Function *calledFun = call.getCalledFunction();
          if (getPatchpointType(calledFun).producesStackmapRecords()) {
            callInsts.push_back(&inst);
          }
        }
      }
    }
    for (auto inst: callInsts) {
      BasicBlock *bb = inst->getParent();
      IRBuilder<> builder(inst);
      FunctionType *funType = FunctionType::get(builder.getVoidTy(), false);
      // Insert empty `asm` blocks.
      InlineAsm *inlineAsm = InlineAsm::get(funType,
                                            "",      /* AsmString */
                                            ""       /* Constraints */,
                                            true     /* hasSideEffects */,
                                            false    /* isAlignStack */);
      if (inst->getPrevNode()) {
        builder.SetInsertPoint(inst->getPrevNode());
        builder.CreateCall(inlineAsm, {});
      }
      if (inst->getNextNode()) {
        builder.SetInsertPoint(inst->getNextNode());
        builder.CreateCall(inlineAsm, {});
      }
    }
    return true;
  }
};

} // end anonymous namespace

char BarrierPass::id = 0;

static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new BarrierPass());
}
static RegisterStandardPasses RegisterPass(
    PassManagerBuilder::EP_EarlyAsPossible, registerPass);
