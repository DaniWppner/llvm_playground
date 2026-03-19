#ifndef HELLOWORLD_MODULE_PASS_H
#define HELLOWORLD_MODULE_PASS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
    class Module;
}

class PlaygroundPass : public llvm::PassInfoMixin<PlaygroundPass> {
public:
    // Notice it now takes a Module and a ModuleAnalysisManager
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

#endif