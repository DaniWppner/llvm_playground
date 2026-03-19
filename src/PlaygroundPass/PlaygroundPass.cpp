#include "PlaygroundPass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

// Required for the plugin registration
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

static void DoSomethingInterestingWithStore(StoreInst *SI) {
    Value *StoredValue = SI->getValueOperand();
    Type *StoredType = StoredValue->getType();
    errs() << "Found a store instruction storing a value of type: " << *StoredType << "\n";

}

PreservedAnalyses PlaygroundPass::run(Module &M, ModuleAnalysisManager &MAM) {
    
    for (auto &F : M) {
        for(auto &BB : F) {
            for (auto &I : BB) {
                if (StoreInst *SI = dyn_cast<StoreInst>(&I))
                    DoSomethingInterestingWithStore(SI);
            }
        }

    }

    // We didn't change anything, so preserve all analyses
    return PreservedAnalyses::all();
}

PassPluginLibraryInfo getPlaygroundPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "PlaygroundPass", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    // This is the flag you will use with `opt`, e.g., -passes=playground-pass
                    if (Name == "playground-pass") {
                        MPM.addPass(PlaygroundPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getPlaygroundPassPluginInfo();
}