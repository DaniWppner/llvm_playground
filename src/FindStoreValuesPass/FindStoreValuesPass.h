#ifndef FIND_STORE_VALUES_PASS_H
#define FIND_STORE_VALUES_PASS_H

#include "llvm/IR/PassManager.h"


class FindStoreValuesPass : public llvm::PassInfoMixin<FindStoreValuesPass> {
public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

#endif