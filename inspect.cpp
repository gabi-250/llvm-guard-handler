#include <cstdio>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Object.h>
#include <llvm-c/Core.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/BitWriter.h>
#include <llvm/CodeGen/MachineFunction.h>

LLVMModuleRef create_module(char *filename) {
   LLVMMemoryBufferRef buf;
   char *error = NULL;
   if (LLVMCreateMemoryBufferWithContentsOfFile(filename, &buf, &error)) {
      printf("%s\n", error);
      exit(1);
   }

   LLVMModuleRef mod = LLVMModuleCreateWithName("test");
   if (LLVMParseBitcode(buf, &mod, &error)) {
      printf("%s", error);
      exit(1);
   }
   return mod;
}

int main(int argc, char **argv) {
   char *error = NULL;
   LLVMMemoryBufferRef out_buf;
   if (argc < 2) {
      printf("Please provide an input file\n");
      exit(1);
   }

   LLVMModuleRef mod = create_module(argv[1]);
   LLVMLinkInMCJIT();
   LLVMInitializeNativeTarget();
   LLVMInitializeNativeAsmPrinter();

   LLVMExecutionEngineRef ee;
   if (LLVMCreateExecutionEngineForModule(&ee, mod, &error)) {
      printf("%s\n", error);
      exit(1);
   }
   LLVMTargetMachineRef t = LLVMGetExecutionEngineTargetMachine(ee);
   if (!t) {
      printf("Failed to extract the target machine\n");
      exit(1);
   }
   LLVMTargetMachineEmitToMemoryBuffer(t, mod, LLVMObjectFile, &error, &out_buf);
   LLVMValueRef fun = LLVMGetNamedFunction(mod, "f");

   LLVMGenericValueRef args[] = {
       LLVMCreateGenericValueOfInt(LLVMInt32Type(), 10, 1)
   };
   LLVMRunFunction(ee, fun, 1, args);
   return 0;
}
