#include "FindStoreValuesPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include <vector>

#define GREEN   "\033[32m"
#define RED     "\033[31m"
#define RESET   "\033[0m"

using namespace llvm;


typedef std::pair<bool,std::vector<std::vector<int>>> retType;

static retType HandleTypeRecursive(Type *Ty);

static retType RecursivelyHandlePointerType(PointerType *Ty) {
    Type *OrigType = Ty->getPointerElementType();
    return HandleTypeRecursive(OrigType);
}

static retType RecursivelyHandleStructType(StructType *OriginalStruct){
    unsigned numElements = OriginalStruct->getNumElements();
    bool any = false;
    std::vector<std::vector<int>> all_offsets = {};
    for (unsigned i = 0; i < numElements; i++){
        Type *ElemType = OriginalStruct->getElementType(i);
        retType res_Pos = HandleTypeRecursive(ElemType);
        any = any || res_Pos.first;
        std::vector<std::vector<int>> this_offsets = res_Pos.second;
        if (any){
          if (this_offsets.size() == 0){
              this_offsets.push_back(std::vector<int>{});
          }
          for (int j = 0; j < this_offsets.size(); j++){
              this_offsets[j].insert(this_offsets[j].begin(), i);
          }
          all_offsets.insert(all_offsets.end(), this_offsets.begin(), this_offsets.end());
        }
    }
    return std::make_pair(any, all_offsets);
}

static retType HandleTypeRecursive(Type *Ty) {
    if (!(Ty->isPointerTy() || Ty->isStructTy())){
        bool result;
        if (Ty->isFunctionTy()){
            result = true;
        }else{
            result = false;
        }
        return std::make_pair(result, std::vector<std::vector<int>>{});
    }
    if (Ty->isPointerTy())
        return RecursivelyHandlePointerType(dyn_cast<PointerType>(Ty));
    else if (StructType* OriginalStruct = dyn_cast<StructType>(Ty))
        return RecursivelyHandleStructType(OriginalStruct);
}

static void ShowResults(retType res, Type *ty, Value *memAddress, DebugLoc loc, const char color[]){
      if (res.first){
            errs() << color << "[LINE " << loc.getLine() << "]" << RESET << " Store of type: " << *ty << " at: " << *memAddress <<"\n";
            //errs() << "Number of offsets: " << res.second.size() << "\n";
            //errs() << "Is there pointer: " << res.first << "\n";
            errs() << GREEN ;
            for (int i = 0; i < res.second.size(); i++){
                errs() << "\tOffset to function pointer: ";
                for (int j = 0; j < res.second[i].size(); j++){
                    errs() << res.second[i][j] << " ";
                }
                errs() <<"\n";
            }
            errs() << RESET ;
        }
}

/*Unused, for debugging purposes*/
static void printTheOneIWant(StoreInst *SI) {
    Value *StoredValue = SI->getValueOperand();
    Value *StoredLocation = SI->getPointerOperand();
    Type *StoredType = StoredValue->getType();
    if (DebugLoc loc = SI->getDebugLoc()){
      if (loc.getLine() == 58 ){
            Value *strippedValue = StoredValue->stripPointerCastsAndAliases();
            Type *strippedType = strippedValue->getType();
            errs() << "[LINE " << loc.getLine() << "] Store of type: " << *StoredType << " at: " << *StoredLocation <<" of value: "<< *StoredValue <<"\n";
            errs() << "[LINE " << loc.getLine() << "] Store of type: " << *strippedType << " at: " << *StoredLocation <<"\n";
        }
      }
}

static void PrintFunctionPointerStores(StoreInst *SI) {
    Value *StoredValue = SI->getValueOperand();
    Value *StoredLocation = SI->getPointerOperand();
    Type *StoredType = StoredValue->getType();
    if (DebugLoc loc = SI->getDebugLoc()){
        retType offsetsToFunctionPointers = HandleTypeRecursive(StoredType);
        ShowResults(offsetsToFunctionPointers, StoredType, StoredLocation, loc, GREEN);
        Value *stripped = StoredValue->stripPointerCastsAndAliases();
        
        Type* strippedType = stripped->getType();
        retType offsetsOfStripped = HandleTypeRecursive(strippedType);
        ShowResults(offsetsOfStripped, strippedType, StoredLocation, loc, RED);
  }
}

PreservedAnalyses FindStoreValuesPass::run(Module &M, ModuleAnalysisManager &MAM) {
    for (auto &F : M) {
        for(auto &BB : F) {
            for (auto &I : BB) {
                if (StoreInst *SI = dyn_cast<StoreInst>(&I))
                    PrintFunctionPointerStores(SI);
            }
        }

    }

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
