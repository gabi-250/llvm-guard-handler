#include <vector>
#include <map>
#include <stdint.h>
#include <llvm/Pass.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
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
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#define GUARD_FUN_NAME "__guard_failure"
#define UNOPT_PREFIX "__unopt_"

using namespace llvm;
using std::vector;
using std::map;
using std::pair;

namespace {

/*
 * Insert a `stackmap` call after each call to record its return address.
 *
 * The unoptimized version of each function contains the same number of
 * `llvm.experimental.stackmap` calls as the original (user-defined) version.
 * Knowing the ID of a `stackmap` call in the optimized version of the function,
 * the ID of the corresponding call in the unoptimized version can be obtained
 * by calculating the logical negation of the ID.
 */
struct CheckPointPass: public FunctionPass {
  static char id;

  // Map each function name to the patchpoint IDs of the stackmap calls
  // in the function. This is used to ensure the runtime can associate the
  // stackmap calls inside each function with stackmap calls from the
  // unoptimized version of the function.
  static map<StringRef, vector<uint64_t>> stackMaps;

  CheckPointPass() : FunctionPass(id) {}

  virtual bool doInitialization(Module &mod) {
    // Declare the guard failure function. This is necessary because the
    // `__guard_failure` function is not defined in the current module.
    LLVMContext &ctx = mod.getContext();
    Type* i64 = Type::getInt64Ty(ctx);
    FunctionType* signature = FunctionType::get(Type::getVoidTy(ctx),
                                                i64, false);
    Function *stackmap_func = Function::Create(
        signature, Function::ExternalLinkage, "__guard_failure", &mod);
    return true;
  }

  virtual bool runOnFunction(Function &fun) {
    Module *mod = fun.getParent();
    StringRef funName = fun.getName();
    outs() << "Running CheckPointPass on function: " << funName << '\n';
    Type *i8ptr_t = PointerType::getUnqual(
        IntegerType::getInt8Ty(mod->getContext()));
    vector <Instruction *> callInsts;
    // The callback to call when a guard fails.
    Constant* guardHandler = ConstantExpr::getBitCast(
        mod->getFunction(GUARD_FUN_NAME), i8ptr_t);
    for (auto &bb : fun) {
      for (BasicBlock::iterator it = bb.begin(); it != bb.end(); ++it) {
        // XXX This is where the guard must fail (an arbitrary instruction in
        // the "more_indirection" function).
        if (funName.endswith("more_indirection") && isa<ReturnInst>(it)) {
          LLVMContext &ctx = bb.getContext();
          IRBuilder<> builder(&bb, it->getIterator());
          uint64_t PPID = getNextPatchpointID(funName);
          // The first two arguments of a stackmap/patchpoint intrinsic call
          // are the unique identifier of the call, and the number of bytes
          // in the shadow of the call.
          auto args = vector<Value*> { builder.getInt64(PPID),
                                       builder.getInt32(13) // XXX shadow
                                      };
          // The intrinsic to call: patchpoint, if the current function is
          // optimized; stackmap, if the current function is unoptimized.
          Function *intrinsic = nullptr;
          if (!funName.startswith(UNOPT_PREFIX)) {
            // The current function is an optimized one -> insert a patchpoint
            // call. The arguments of the patchpoint call are: a callback, an
            // integer which represents the number of arguments of the
            // callback, and the arguments to pass to the callback.
            args.insert(args.end(),
                        { guardHandler,          // the callback
                          builder.getInt32(1),   // the callback has 1 argument
                          builder.getInt64(PPID) // the argument
                        });
            intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_patchpoint_void);
          } else {
            // The function is not optimized -> insert a patchpoint call with
            // no callback
            auto callback = builder.CreateIntToPtr(builder.getInt64(0),
                                                   builder.getInt8PtrTy());
            args.insert(args.end(),
                        { callback,              // no callback
                          builder.getInt32(0),   // the callback has no arguments
                        });
            intrinsic = Intrinsic::getDeclaration(
                mod, Intrinsic::experimental_patchpoint_void);
          }
          auto callInst = builder.CreateCall(intrinsic, args);
        } else if (isa<CallInst>(it)) {
          CallInst &oldCallInst = cast<CallInst>(*it);
          Function *calledFun = oldCallInst.getCalledFunction();
          if (!oldCallInst.isInlineAsm() &&
              !calledFun->hasAvailableExternallyLinkage() &&
              !calledFun->isDeclaration()) {
            // This is a function call. A guard might fail inside the called
            // function, so its return address must be recorded.
            uint64_t PPID = getNextPatchpointID(funName);
            IRBuilder<> builder(it->getNextNode());
            auto args = vector<Value*> { builder.getInt64(PPID),
                                         builder.getInt32(13)
                                       };
            callInsts.push_back(&*it);
            if (funName.startswith(UNOPT_PREFIX)) {
              // The current function is an unoptimized one, so we must replace
              // all calls inside it with patchpoint calls. The originally
              // called functions will be passed as callbacks to the patchpoint
              // calls. This enables the runtime to accurately identify the
              // return addresses of the calls.
              if (!calledFun->getName().startswith(UNOPT_PREFIX)) {
                calledFun = mod->getFunction(getTwinName(calledFun->getName()));
              }
              // The callback to pass to the patchpoint call.
              Constant* callback = ConstantExpr::getBitCast(calledFun, i8ptr_t);
              args.insert(args.end(),
                          { callback,          // the callback
                            builder.getInt32(oldCallInst.getNumArgOperands())
                          });
              for (unsigned i = 0; i < oldCallInst.getNumArgOperands(); ++i) {
                args.push_back(oldCallInst.getArgOperand(i));
              }
              // Find the correct `llvm.experimental.patchpoint.*` to call,
              // according to the return value of the called function.
              Function *intrinsic = nullptr;
              if (calledFun->getReturnType()->isVoidTy()) {
                intrinsic = Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_patchpoint_void);
              } else {
                intrinsic = Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_patchpoint_i64);
              }
              // Recreate this call instruction as a patchpoint call.
              auto callInst = CallInst::Create(intrinsic, args);
              // Replace the uses of this call instruction with the newly
              // created patchpoint call.
              auto calledFunRetTy = calledFun->getReturnType();
              if (calledFunRetTy->isVoidTy()) {
                if (!oldCallInst.use_empty()) {
                  oldCallInst.replaceAllUsesWith(callInst);
                }
              } else {
                auto retTy = oldCallInst.getCalledFunction()->getReturnType();
                Value *retValue = nullptr;
                if (calledFunRetTy->isIntegerTy()) {
                  retValue = builder.CreateTruncOrBitCast(callInst, retTy);
                } else {
                  auto floatPtrTy = PointerType::getUnqual(retTy);
                  retValue = builder.CreateIntToPtr(callInst, floatPtrTy);
                  retValue = builder.CreateLoad(retValue, retTy);
                }
                if (!oldCallInst.use_empty()) {
                  oldCallInst.replaceAllUsesWith(retValue);
                }
              }
              ReplaceInstWithInst(&oldCallInst, callInst);
              it = callInst->getIterator();
            } else {
              // The function is not an __unopt_ function. Insert a  stackmap
              // call after the current call instruction to record the return
              // address of the call. Patchpoint callbacks are never inlined,
              // which is why we must use `stackmap` calls in the optimized
              // versions of the functions.
              auto intrinsic = Intrinsic::getDeclaration(
                  mod, Intrinsic::experimental_stackmap);
              builder.CreateCall(intrinsic, args);
            }
          }
        }
      }
    }

    for (auto inst: callInsts) {
      IRBuilder<> builder(inst);
      FunctionType *funType = FunctionType::get(builder.getVoidTy(), false);
      // Insert empty `asm` blocks.
      InlineAsm *inlineAsm = InlineAsm::get(funType,
                                            "",      /* AsmString */
                                            ""       /* Constraints */,
                                            true     /* hasSideEffects */,
                                            false    /* isAlignStack */);
      builder.CreateCall(inlineAsm, {});
    }
    return true;
  }

  /*
   * Return the name of the 'twin' of the specified function.
   *
   * If the specified name is the name of an optimized function, then the name
   * of the function's 'twin' is the name prefixed with '__unopt_'. If the
   * specified name is the name of an unoptimized function, then this returns
   * the name of the optimized function (it strips off the '__unopt_' prefix).
   *
   * Example: "fun" -> "__unopt_fun" or "__unopt_fun" -> "fun"
   */
  static std::string getTwinName(StringRef name) {
    if (name.startswith(UNOPT_PREFIX)) {
      return name.drop_front(StringRef(UNOPT_PREFIX).size()).str();
    }
    return UNOPT_PREFIX + name.str();
  }

  /*
   * Generate a new patchpoint ID for the specified function.
   */
  static uint64_t getNextPatchpointID(StringRef funName) {
    uint64_t nextPatchpointID = 0;
    if (!stackMaps.empty()) {
        std::string twinFun = getTwinName(funName);
        if (stackMaps.find(twinFun) == stackMaps.end()) {
          // The `twin` of the function doesn't have any IDs allocated yet.
          // This means the next ID is equal to the last ID generated for the
          // current function plus one.
          auto maxPos = std::max_element(stackMaps.begin(), stackMaps.end(),
              [] (const pair<StringRef, vector<uint64_t>> &a,
                    const pair<StringRef, vector<uint64_t>> &b) {
                return a.second.back() < b.second.back();
              });
          nextPatchpointID = (*maxPos).second.back() + 1;
        } else {
          // Find (in the `twin` function) the ID of the `stackmap` which
          // corresponds to the ID currently being generated. Negate the ID
          // found, to obtain a new ID for the current function.
          size_t lastIndex = stackMaps[funName].size();
          nextPatchpointID = ~stackMaps[twinFun][lastIndex];
        }
    }
    if (stackMaps.find(funName) == stackMaps.end() &&
        stackMaps.find(getTwinName(funName)) == stackMaps.end()) {
      // This ensures `stackmap`/`patchpoint` calls which belong to the same
      // function have consecutive identifiers. When a new function is found,
      // `nextPatchpointID` has to be incremented.
      ++nextPatchpointID;
    }
    stackMaps[funName].push_back(nextPatchpointID);
    return nextPatchpointID;
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
