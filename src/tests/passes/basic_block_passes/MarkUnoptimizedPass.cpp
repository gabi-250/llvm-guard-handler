#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Constant.h>

#define UNOPT_PREFIX "__unopt_"

using namespace llvm;

namespace {

/*
 * Replace the return address of some of the '__unopt_' functions.
 *
 * This is only used for testing purposes: if the unoptimized version of a
 * function returns a different value than its optimized version, then it is
 * easy to check if control reaches the '__unopt_' functions.
 */
struct MarkUnoptimizedPass: public BasicBlockPass {
  static char id;

  MarkUnoptimizedPass() : BasicBlockPass(id) {}

  /*
   * Replace the return values of __unopt_ functions.
   */
  virtual bool runOnBasicBlock(BasicBlock &bb) {
    Function *fun = bb.getParent();
    auto funName = fun->getName();
    if (!funName.startswith(UNOPT_PREFIX)) {
      return false;
    }
    outs() << "Running MarkUnoptimizedPass on function: " << funName << '\n';
    for (auto &inst : bb) {
      if (isa<ReturnInst>(inst) && inst.getNumOperands() == 1) {
        outs() << inst << '\n';
        IRBuilder<> builder(bb.getContext());
        ReturnInst &retInst = cast<ReturnInst>(inst);
        // Replace the return value with something else (100 in this case).
        int newRetVal = 100;
        retInst.setOperand(0, builder.getInt32(newRetVal));
      }
    }
    return true;
  }
};

} // end anonymous namespace

char MarkUnoptimizedPass::id = 0;

static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new MarkUnoptimizedPass());
}
static RegisterStandardPasses RegisterPass(
    PassManagerBuilder::EP_EarlyAsPossible, registerPass);
