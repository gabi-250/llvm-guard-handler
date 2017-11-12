#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/MCJIT.h>

using namespace llvm;

std::unique_ptr<Module> getModule(const std::string &filename, LLVMContext &ctx) {
    SMDiagnostic error;
    std::unique_ptr<Module> mod = parseIRFile(filename, error, ctx);
    if (!mod) {
        std::cerr << error.getMessage().str() << '\n';
        return nullptr;
    }
    return mod;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Please enter an input file\n";
        return 1;
    }
    LLVMContext ctx;
    std::unique_ptr<Module> mod = getModule(argv[1], ctx);
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    std::string error;
    ExecutionEngine *engine = EngineBuilder(std::move(mod))
          .setEngineKind(EngineKind::JIT).setErrorStr(&error)
          .create();
    if (!engine) {
        std::cerr<< error <<'\n';
        exit(1);
    }
    Function *fun = engine->FindFunctionNamed("f");

    std::vector<GenericValue> args;
    GenericValue val;
    val.IntVal = APInt(32, 5); // a 32 bit value (5)
    args.push_back(val);
    GenericValue ret = engine->runFunction(fun, args);
    std::cout << engine->getDataLayout().getStringRepresentation() << '\n';
    return 0;
}
