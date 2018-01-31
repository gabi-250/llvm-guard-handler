#include <vector>
#include <map>
#include <stdint.h>
#include <llvm/Pass.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#define TRACE_FUN_NAME "get_number"
#define GUARD_FUN_NAME "__guard_failure"
#define UNOPT_PREFIX "__unopt_"

using namespace llvm;

namespace {

/*
 * Insert a stackmap call before each call to `putchar`.
 *
 * This only runs on `trace` and `__unopt_trace`. All other functions are
 * ignored.
 */
struct CheckPointPass: public FunctionPass {
  static char id;

  // The unique identifier of the next stackmap call.
  static uint64_t sm_id;

  // Map the function name to the ID of the first stackmap/patchpoint call
  // in the function. This is used to ensure the runtime can associate the
  // stackmap calls inside each function with stackmap calls from the
  // unoptimized version of the function
  static std::map<StringRef, uint64_t> start_sm_id;

  CheckPointPass() : FunctionPass(id) {}

  virtual bool runOnFunction(Function &fun) {
    StringRef fun_name = fun.getName();
    if (!fun_name.endswith(TRACE_FUN_NAME) && !fun_name.endswith("trace")) {
      return false;
    }
    outs() << "Running CheckPointPass on function: " << fun.getName() << '\n';

    // Each function has a corresponding unoptimized version, which starts
    // with the `__unopt_` prefix (the 'twin').
    StringRef twin_fun = getTwinName(fun_name);

    // The next stackmap ID to assign
    uint64_t curr_sm_id = sm_id;
    if (start_sm_id.find(twin_fun) != start_sm_id.end()) {
      // If the 'twin' of this function has already been processed, index
      // stackmap calls starting from ~ID, where ID is the identifier of the
      // first stackmap call in the 'twin'
      curr_sm_id = ~start_sm_id[twin_fun];
    }

    Module *mod = fun.getParent();
    DataLayout data_layout(mod);
    // sm_id is the ID of the first stackmap call in this function
    start_sm_id[fun_name] = sm_id;
    uint64_t call_inst_count = 0;


    Type *i8ptr_t = PointerType::getUnqual(
        IntegerType::getInt8Ty(mod->getContext()));
    // The callback to call when a guard fails
    Constant* gf_handler_ptr = ConstantExpr::getBitCast(
        mod->getFunction(GUARD_FUN_NAME), i8ptr_t);
    for (auto &bb : fun) {
      for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
        if (isa<ReturnInst>(it)) {
          LLVMContext &ctx = bb.getContext();
          IRBuilder<> builder(&bb, it);
          uint64_t patchpoint_id;

          if (start_sm_id.find(twin_fun) == start_sm_id.end()) {
            // Each stackmap ID is calculated by adding the offset
            // (call_inst_count) to the 'base' ID (curr_sm_id)
            patchpoint_id = curr_sm_id + call_inst_count;
          } else {
            // If the 'twin' of this function has already been processed,
            // make sure the ID of each stackmap call in this function can be
            // obtained from an ID of a stackmap call in the 'twin', by
            // negating the ID. For example, the patch point with ID `0`
            // corresponds to the patchpoint with ID `~0 = -1`.
            patchpoint_id = ~(~curr_sm_id + call_inst_count);
          }
          // The first two arguments of a stackmap/patchpoint intrinsic call
          // must be the unique identifier of the call and the number of bytes
          // in the shadow of the call
          auto args = std::vector<Value*> { builder.getInt64(patchpoint_id),
                                            builder.getInt32(13) // XXX shadow
                                          };
          // The intrinsic to call: patchpoint, if the current function is
          // trace; stackmap, if the current function is __unopt_trace
          Function *intrinsic = nullptr;

          if (!fun_name.startswith(UNOPT_PREFIX)) {
            // The current function is trace -> insert a patchpoint call. The
            // arguments of the patchpoint call are: a callback, an integer
            // which represents the number of arguments of the callback, and
            // the arguments to pass to the callback.
            args.insert(args.end(),
                        { gf_handler_ptr,      // the callback
                          builder.getInt32(1), // the callback has one argument
                          builder.getInt64(patchpoint_id) // the argument
                        });
            intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_patchpoint_void);
          } else {
            // The function is __unopt_trace -> insert a stackmap call instead
            // of a patchpoint call
            intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_stackmap);
          }
          // Pass the live locations to the stackmap/patchpoint call. This also
          // inserts the size of each 'live' location into the stackmap.
          for (BasicBlock::iterator prev_inst = bb.begin(); prev_inst != it;
               ++prev_inst) {
            // XXX need to accurately identify the live registers - this
            // currently assumes each instruction in the current basic block to
            // represent a live location.
            if (prev_inst != it &&
                  prev_inst->use_begin() != prev_inst->use_end()) {
              // Only look at instructions which produce values that have at
              // least one use. If a result is not used anywhere, it is going
              // to be removed by future optimization passes, so it should not
              // be recorded.
              args.push_back(&*prev_inst);
              auto size_of_inst = builder.getInt64(8); // XXX default size
              if (isa<AllocaInst>(prev_inst)) {
                Type *t = cast<AllocaInst>(*prev_inst).getAllocatedType();
                size_of_inst = builder.getInt64(
                    data_layout.getTypeAllocSize(t));
              }
              // also insert the size of the recorded location to know how
              // many bytes to copy at runtime
              args.push_back(size_of_inst);
            }
          }
          auto call_inst = builder.CreateCall(intrinsic, args);
          ++call_inst_count;
        } else if (isa<CallInst>(it)) {
          Function *called_fun = cast<CallInst>(*it).getCalledFunction();
          if (!called_fun->hasAvailableExternallyLinkage() &&
              !called_fun->isDeclaration()) {
            outs() << "Adding SM before " << called_fun->getName() << '\n';
            uint64_t patchpoint_id;
            if (start_sm_id.find(twin_fun) == start_sm_id.end()) {
              patchpoint_id = curr_sm_id + call_inst_count;
            } else {
              patchpoint_id = ~(~curr_sm_id + call_inst_count);
            }
            // XXX insert after
            IRBuilder<> builder(&bb, ++it);
            auto args = std::vector<Value*> { builder.getInt64(patchpoint_id),
                                              builder.getInt32(13) // XXX shadow
                                            };
            auto intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_stackmap);

            auto call_inst = builder.CreateCall(intrinsic, args);
            ++call_inst_count;
          }
        }
      }
    }
    if (start_sm_id.find(twin_fun) == start_sm_id.end()) {
      // The 'twin' of the function has not yet been processed, which means
      // sm_id was used as a 'base' ID, so it needs to be updated, since
      // future functions can no longer use it as a 'base'.
      sm_id = start_sm_id[fun_name] + call_inst_count;
    }
    return true;
  }

  /*
   * Return the name of 'twin' function of function with the specified name.
   *
   * If name is the name of an optimized function, then the name of the
   * function's 'twin' is the name prefixed with '__unopt_'. If the specified
   * name is the name of an unoptimized function, then this returns the name
   * of the optimized function (it strips off the '__unopt_' prefix).
   *
   * "fun" -> "__unopt_fun" or "__unopt_fun" -> "fun"
   */
  static StringRef getTwinName(StringRef name) {
    if (name.startswith(UNOPT_PREFIX)) {
      return name.substr(StringRef(UNOPT_PREFIX).size());
    }
    return StringRef(UNOPT_PREFIX + name.str());
  }

};

} // end anonymous namespace

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
