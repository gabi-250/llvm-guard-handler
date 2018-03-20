#include <vector>
#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InlineAsm.h>
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
 * Split the basic blocks around each callsite marked by a `stackmap` call.
 *
 * An earlier pass is supposed to insert an empty `asm` block before each
 * function call. `CheckPointPass` inserts a `stackmap` call after each call
 * instruction. This pass splits the basic blocks around each call site:
 * before the `asm` block, and after the `stackmap` call.
 *
 * The basic blocks must be split to prevent global values from being
 * loaded at an inconvenient point.
 *
 * br label %15

 * ; <label>:15:
 * tail call void asm sideeffect "", ""() #4
 * %16 = tail call i32 @get_number(i32 %14)
 * call void (i64, i32, ...) @llvm.experimental.stackmap(i64 3, i32 13, ...)
 *
 * br label %17
 * <label>:17:
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
        if (call.isInlineAsm()) {
          if (inst.getPrevNode()) {
            callInsts.push_back(&inst);
          }
        } else {
          Function *calledFun = call.getCalledFunction();
          if (getPatchpointType(calledFun).producesStackmapRecords()) {
            if (inst.getNextNode()) {
              callInsts.push_back(inst.getNextNode());
            }
          }
        }
      }
    }
    for (auto inst: callInsts) {
      BasicBlock *bb = inst->getParent();
      bb->splitBasicBlock(inst);
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
    PassManagerBuilder::EP_OptimizerLast, registerPass);
