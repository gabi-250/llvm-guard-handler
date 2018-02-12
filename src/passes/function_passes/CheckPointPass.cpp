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
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Dominators.h>

#define TRACE_FUN_NAME "trace"
#define GUARD_FUN_NAME "__guard_failure"
#define UNOPT_PREFIX "__unopt_"

using namespace llvm;
using std::vector;
using std::map;
using std::pair;

namespace {

/*
 * Insert a `stackmap` call after each call to record its return address.
 */
struct CheckPointPass: public FunctionPass {
  static char id;

  // Map the function name to the patchpoint IDs of the stackmap calls
  // in the function. This is used to ensure the runtime can associate the
  // stackmap calls inside each function with stackmap calls from the
  // unoptimized version of the function
  static map<StringRef, vector<uint64_t>> stackMaps;

  CheckPointPass() : FunctionPass(id) {}

  virtual bool doInitialization(Module &mod) {
    // Insert the guard failure function.
    LLVMContext &ctx = mod.getContext();
    Type* i64 = Type::getInt64Ty(ctx);
    FunctionType* signature = FunctionType::get(Type::getVoidTy(ctx),
                                                i64, false);
    Function *stackmap_func = Function::Create(
        signature, Function::ExternalLinkage, "__guard_failure", &mod);
    return true;
  }

  virtual bool runOnFunction(Function &fun) {
    StringRef funName = fun.getName();
    outs() << "Running CheckPointPass on function: " << fun.getName() << '\n';
    Module *mod = fun.getParent();
    Type *i8ptr_t = PointerType::getUnqual(
        IntegerType::getInt8Ty(mod->getContext()));
    // The callback to call when a guard fails
    Constant* guardHanlder = ConstantExpr::getBitCast(
        mod->getFunction(GUARD_FUN_NAME), i8ptr_t);
    for (auto &bb : fun) {
      for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
        if (funName.endswith("more_indirection") && isa<ReturnInst>(it)) {
          LLVMContext &ctx = bb.getContext();
          IRBuilder<> builder(&bb, it);
          uint64_t PPID = getNextPatchpointID(funName);
          // The first two arguments of a stackmap/patchpoint intrinsic call
          // must be the unique identifier of the call and the number of bytes
          // in the shadow of the call
          auto args = vector<Value*> { builder.getInt64(PPID),
                                       builder.getInt32(13) // XXX shadow
                                      };
          // The intrinsic to call: patchpoint, if the current function is
          // trace; stackmap, if the current function is __unopt_trace
          Function *intrinsic = nullptr;

          if (!funName.startswith(UNOPT_PREFIX)) {
            // The current function is trace -> insert a patchpoint call. The
            // arguments of the patchpoint call are: a callback, an integer
            // which represents the number of arguments of the callback, and
            // the arguments to pass to the callback.
            args.insert(args.end(),
                        { guardHanlder,          // the callback
                          builder.getInt32(1),   // the callback has one argument
                          builder.getInt64(PPID) // the argument
                        });
            intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_patchpoint_void);
          } else {
            // The function is __unopt_trace -> insert a stackmap call instead
            // of a patchpoint call
            intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_stackmap);
          }

          auto liveVariables = getLiveRegisters(bb, it);
          // Pass the live locations to the stackmap/patchpoint call. This also
          // inserts the size of each 'live' location into the stackmap.
          std::move(liveVariables.begin(), liveVariables.end(),
                    std::back_inserter(args));

          auto call_inst = builder.CreateCall(intrinsic, args);
        } else if (isa<CallInst>(it)) {
          // Insert a stackmap call after each call in which a guard may
          // fail, in order to be able to work out the correct return address
          Function *calledFun = cast<CallInst>(*it).getCalledFunction();
          if (!calledFun->hasAvailableExternallyLinkage() &&
              !calledFun->isDeclaration()) {
            uint64_t PPID = getNextPatchpointID(funName);
            // XXX insert after
            IRBuilder<> builder(&bb, ++it);
            auto args = vector<Value*> { builder.getInt64(PPID),
                                         builder.getInt32(13)  // XXX shadow
                                       };
            auto liveVariables = getLiveRegisters(bb, it);
            std::move(liveVariables.begin(), liveVariables.end(),
                      std::back_inserter(args));
            auto intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_stackmap);

            auto call_inst = builder.CreateCall(intrinsic, args);
          }
        }
      }
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
  static std::string getTwinName(StringRef name) {
    if (name.startswith(UNOPT_PREFIX)) {
      return name.drop_front(StringRef(UNOPT_PREFIX).size()).str();
    }
    return UNOPT_PREFIX + name.str();
  }

  static uint64_t getNextPatchpointID(StringRef funName) {
    uint64_t nextPatchpointID = 0;
    if (!stackMaps.empty()) {
        std::string twinFun = getTwinName(funName);
        if (stackMaps.find(twinFun) == stackMaps.end()) {
          auto maxPos = std::max_element(stackMaps.begin(), stackMaps.end(),
              [] (const pair<StringRef, vector<uint64_t>> &a,
                    const pair<StringRef, vector<uint64_t>> &b) {
                return a.second.back() < b.second.back();
              });
          nextPatchpointID = (*maxPos).second.back() + 1;
        } else {
          size_t lastIndex = stackMaps[funName].size();
          nextPatchpointID = ~stackMaps[twinFun][lastIndex];
        }
    }
    stackMaps[funName].push_back(nextPatchpointID);
    return nextPatchpointID;
  }

  static vector<Value *> getLiveRegisters(BasicBlock &bb,
                                          const BasicBlock::iterator &smIt) {
    vector<Value *> args;
    Function *fun = bb.getParent();
    Module *mod = fun->getParent();
    DataLayout dataLayout(mod);
    DominatorTree DT(*fun);
    for (auto &bb : *fun) {
      IRBuilder<> builder(bb.getContext());
      for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
        if (!(*it).use_empty() && !DT.dominates(&(*smIt), &*it)) {
          for (Value::use_iterator use = (*it).use_begin();
               use != (*it).use_end(); ++use) {
            if (DT.dominates(&(*smIt), *use)) {
              args.push_back(&*it);
              auto instSize = builder.getInt64(8); // XXX default size
              if (isa<AllocaInst>(it)) {
                Type *t = cast<AllocaInst>(*it).getAllocatedType();
                instSize = builder.getInt64(
                    dataLayout.getTypeAllocSize(t));
              }
              // also insert the size of the recorded location to know how
              // many bytes to copy at runtime
              args.push_back(instSize);
            }
          }
        }
      }
    }
    return args;
  }

};

} // end anonymous namespace

char CheckPointPass::id = 0;
map<StringRef, vector<uint64_t>> CheckPointPass::stackMaps;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new CheckPointPass());
}
static RegisterStandardPasses RegisterPass(
PassManagerBuilder::EP_EarlyAsPossible, registerPass);
