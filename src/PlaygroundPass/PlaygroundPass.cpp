#include "PlaygroundPass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

// Required for the plugin registration
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <string>

using namespace llvm;

#define GREEN   "\033[32m"
#define RED     "\033[31m"
#define RESET   "\033[0m"


static std::string prependString(unsigned Depth){
    return std::string(Depth, '\t');
}

static void RecursivelyInformOnType(Type *Ty, unsigned Depth);


static void RecursivelyHandlePointerType(PointerType *Ty, unsigned Depth) {
    errs() << prependString(Depth) << *Ty << " IS POINTER TYPE\n";
    Type *OrigType = Ty->getPointerElementType();
    Depth += 1;
    RecursivelyInformOnType(OrigType, Depth);
}

static void RecursivelyHandleStructType(StructType *OriginalStruct, unsigned Depth){
    errs() << GREEN << prependString(Depth) << *OriginalStruct << " IS STRUCT TYPE\n" << RESET;
    Depth += 1;
    unsigned numElements = OriginalStruct->getNumElements();
    for (unsigned i = 0; i < numElements; i++){
        Type *ElemType = OriginalStruct->getElementType(i);
        RecursivelyInformOnType(ElemType, Depth);
    }
}

static void RecursivelyHandleArrayType(ArrayType *OriginalArray, unsigned Depth){
    errs() << prependString(Depth) << *OriginalArray << " IS ARRAY TYPE\n";
    Depth += 1;
    Type *ElemType = OriginalArray->getElementType();
    RecursivelyInformOnType(ElemType, Depth);
}

static void DoSomethingInterestingWithStore(StoreInst *SI) {
    Value *StoredValue = SI->getValueOperand();
    Value *StoredLocation = SI->getPointerOperand();
    Type *StoredType = StoredValue->getType();
    if (DebugLoc loc = SI->getDebugLoc()){
        errs() << "[LINE " << loc.getLine() << "] Store of type: " << *StoredType << " at: " << *StoredLocation <<"\n";
            RecursivelyInformOnType(StoredType, 1);
        }
    else{
        errs() << "[NO DEBUG INFO] Store of type: " << *StoredType << " at: " << *StoredLocation << "\n";    
    }
}

static void RecursivelyInformOnType(Type *Ty, unsigned Depth) {
    if (!(Ty->isPointerTy() || Ty->isStructTy() || Ty->isArrayTy())){
        if (Ty->isFunctionTy())
            errs() << GREEN << prependString(Depth) << *Ty << " IS FUNCTION TYPE\n" << RESET;
        return;
    }
    if (Ty->isPointerTy())
        RecursivelyHandlePointerType(dyn_cast<PointerType>(Ty), Depth);
    else if (StructType* OriginalStruct = dyn_cast<StructType>(Ty))
        RecursivelyHandleStructType(OriginalStruct, Depth);
    else if (ArrayType* OriginalArray = dyn_cast<ArrayType>(Ty))
        RecursivelyHandleArrayType(OriginalArray, Depth);
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