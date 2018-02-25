#ifndef FUNCTION_PASS_UTILS_H
#define FUNCTION_PASS_UTILS_H

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

struct PatchpointType {
  unsigned int type : 3;
  static const unsigned int STACKMAP        = 1 << 0;
  static const unsigned int PATCHPOINT_VOID = 1 << 1;
  static const unsigned int PATCHPOINT_I64  = 1 << 2;

  bool isPatchpoint() {
    return (type & PATCHPOINT_VOID) | (type & PATCHPOINT_I64);
  }

  bool producesStackmapRecords() {
    return (type & PATCHPOINT_VOID) | (type & PATCHPOINT_I64) |
           (type & STACKMAP);
  }
};

PatchpointType getPatchpointType(llvm::Function *fun);

#endif // FUNCTION_PASS_UTILS_H
