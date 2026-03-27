#include "FindStoreValuesPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include <vector>


static constexpr const char *RESET = "\033[0m";
static constexpr const char *GREEN = "\033[32m";
static constexpr const char *RED = "\033[31m";
static constexpr const char *YELLOW = "\033[33m";
static constexpr const char *BLUE = "\033[34m";
static constexpr const char *MAGENTA = "\033[35m";
static constexpr const char *CYAN = "\033[36m";
static constexpr const char *WHITE = "\033[37m";
static constexpr const char *GRAY = "\033[90m";
static constexpr const char *COLORS[] = {GREEN, RED, YELLOW, BLUE, MAGENTA, CYAN, WHITE, GRAY};

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


static std::vector<const Value*> GetAllValuesThatWouldBeStripped(const Value *V) {
    SmallPtrSet<const Value *, 8> Visited;
    std::vector<const Value*> res;
    bool ciclicalTypeDetected = false;
    Visited.insert(V);
    res.push_back(V);
    while (!ciclicalTypeDetected) {

      /*
      There are four cases we are interested in
        1. GEPOperator means the pointer can be used to access the same address as a different type
        2. BitCast Instruction is by definition a type change without modifying the data
        3. AddressSpaceCast Instruction moves the pointer to a different address space, and can redefine the pointer type
        4. CallBase Instruction can be interpreted as the Value being of the function return type 
      */      
        if (const GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
          if (!GEP->hasAllZeroIndices())
             // In this case a different memory address is accessed
             // so we cannot handle it as just a type cast
             return res;
          // retrieve the "base" pointer
          V = GEP->getPointerOperand();
        } else if (Operator::getOpcode(V) == Instruction::BitCast) {
            // first operand is the original type
            Value *NewV = cast<Operator>(V)->getOperand(0);
            if (!NewV->getType()->isPointerTy()){
              res.push_back(NewV);
              return res;
            }
            V = NewV;
        } else if (Operator::getOpcode(V) == Instruction::AddrSpaceCast){
            // first operand is the original pointer type
            V = cast<Operator>(V)->getOperand(0);
        } else if (const CallBase *Call = dyn_cast<CallBase>(V)){ 
            if (const Value *RV = Call->getReturnedArgOperand()){
              V = RV;
            } else {
              return res;
            }
        } else {
          // There are no other types we want to handle, this is the end.
          return res;
        }

        assert(V->getType()->isPointerTy() && "Unexpected operand type!");
        res.push_back(V);
        ciclicalTypeDetected = !(Visited.insert(V).second);
    }
    return res;    
}


static void ShowResults(retType res, Type *ty, Value *memAddress, DebugLoc loc, const char *color){
      if (res.first){
            errs() << color << "[LINE " << loc.getLine() << "]" << RESET << " Store of type: " << *ty << " at: " << *memAddress <<"\n";
            //errs() << "Number of offsets: " << res.second.size() << "\n";
            //errs() << "Is there pointer: " << res.first << "\n";
            errs() << color ;
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
    if (DebugLoc loc = SI->getDebugLoc()){
        std::vector<const Value*> TypesOfStoredValue = GetAllValuesThatWouldBeStripped(StoredValue);

        for (int i = 0; i < TypesOfStoredValue.size(); i++){
            Type *StoredType = TypesOfStoredValue[i]->getType();
            retType offsetsToFunctionPointers = HandleTypeRecursive(StoredType);
            const char *color = COLORS[i];
            ShowResults(offsetsToFunctionPointers, StoredType, StoredLocation, loc, color);
        }     
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
