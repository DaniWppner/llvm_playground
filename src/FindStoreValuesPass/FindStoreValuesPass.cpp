#include "FindStoreValuesPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

PreservedAnalyses FindStoreValuesPass::run(Module &M, ModuleAnalysisManager &MAM) {
    return PreservedAnalyses::all();
}

PassPluginLibraryInfo getFindStoreValuesPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "FindStoreValuesPass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  // This is the flag you will use with `opt`, e.g., -passes=playground-pass
                  if (Name == "find-store-values-pass") {
                    MPM.addPass(FindStoreValuesPass());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getFindStoreValuesPassPluginInfo();
}
