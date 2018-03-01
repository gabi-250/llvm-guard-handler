#include "Utils.h"

using namespace llvm;

PatchpointType getPatchpointType(Function *fun) {
  Module *mod = fun->getParent();
  auto stackmapIntrinsic = Intrinsic::getDeclaration(
      mod, Intrinsic::experimental_stackmap);
  auto patchpointIntrinsicVoid = Intrinsic::getDeclaration(
      mod, Intrinsic::experimental_patchpoint_void);
  auto patchpointIntrinsici64 = Intrinsic::getDeclaration(
      mod, Intrinsic::experimental_patchpoint_i64);
  unsigned int type = 0;
  if (fun == stackmapIntrinsic) {
    type = PatchpointType::STACKMAP;
  } else if (fun == patchpointIntrinsicVoid) {
    type = PatchpointType::PATCHPOINT_VOID;
  } else if (fun == patchpointIntrinsici64) {
    type = PatchpointType::PATCHPOINT_I64;
  }
  return PatchpointType{ type };
}
