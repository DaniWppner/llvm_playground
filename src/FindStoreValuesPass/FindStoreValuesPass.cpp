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

typedef std::pair<bool, std::vector<std::vector<int>>> retType;

typedef std::vector<Type *> Types;
typedef std::vector<int> Offsets;

typedef std::vector<
    std::pair<Types, Offsets>>
    vectorTypesOffsets;

typedef std::pair<bool, vectorTypesOffsets> handleTypeRetType;


static void ShowTypesOffsets(vectorTypesOffsets typesOffsets)
{
    for (int i = 0; i < typesOffsets.size(); i++)
    {
        Types this_types = typesOffsets[i].first;
        Offsets this_offsets = typesOffsets[i].second;
        for (int j = 0; j < this_types.size(); j++)
        {
            errs() << std::string('\t', j) << "Type: " << *this_types[j] << " Offset: " << this_offsets[j] << "\n";
        }
        
    }
}

static void ShowPointersWithOffsets(std::vector<int> offsets, Value *storedValue, Value *FunctionPointer, DebugLoc loc)
{
    if (FunctionPointer)
    {
        errs() << "\tFunction pointer value:" << *FunctionPointer << " at offset:\n";
        for (int k = 0; k < offsets.size(); k++)
        {
            errs() << "\t\t" << offsets[k] << "\n";
        }
    }
    else
    {
        errs() << "\t Failed to retrieve function pointer value from " << *storedValue << " at offset:\n";
        for (int k = 0; k < offsets.size(); k++)
        {
            errs() << "\t\t" << offsets[k] << "\n";
        }
    }
}


static handleTypeRetType HandleTypeRecursive(Type *Ty);

static handleTypeRetType RecursivelyHandlePointerType(PointerType *Ty)
{
    Type *OrigType = Ty->getPointerElementType();
    return HandleTypeRecursive(OrigType);
}

static handleTypeRetType RecursivelyHandleStructType(StructType *OriginalStruct)
{
    unsigned numElements = OriginalStruct->getNumElements();
    bool any = false;
    vectorTypesOffsets all_vector_res = {};
    for (unsigned i = 0; i < numElements; i++)
    {
        Type *ElemType = OriginalStruct->getElementType(i);
        handleTypeRetType res_Pos = HandleTypeRecursive(ElemType);
        any = any || res_Pos.first;
        vectorTypesOffsets this_types_offsets = res_Pos.second;
        if (any)
        {
            if (this_types_offsets.size() == 0)
            {
                this_types_offsets.push_back(
                    std::make_pair(Types{}, Offsets{}));
            }
            for (int j = 0; j < this_types_offsets.size(); j++)
            {
                Types this_types_j = this_types_offsets[j].first;
                Offsets this_offsets_j = this_types_offsets[j].second;
                this_types_j.insert(this_types_j.begin(), ElemType);
                this_offsets_j.insert(this_offsets_j.begin(), i);
            }
            all_vector_res.insert(all_vector_res.end(), this_types_offsets.begin(), this_types_offsets.end());
        }
    }
    return std::make_pair(any, all_vector_res);
}

/*
Returns:
  - bool: whether the type can be stripped to a function pointer with some offsets
  - vector: pairs of struct types and offset chains used to access each function pointer.
*/
static handleTypeRetType HandleTypeRecursive(Type *Ty)
{
    if (!(Ty->isPointerTy() || Ty->isStructTy()))
    {
        bool result;
        if (Ty->isFunctionTy())
        {
            result = true;
        }
        else
        {
            result = false;
        }
        vectorTypesOffsets vector_result = {};
        return std::make_pair(result, vector_result);
    }
    if (Ty->isPointerTy())
        return RecursivelyHandlePointerType(dyn_cast<PointerType>(Ty));
    else if (StructType *OriginalStruct = dyn_cast<StructType>(Ty))
        return RecursivelyHandleStructType(OriginalStruct);
    else
    {
        // TODO: handle more types
        errs() << "Unexpected type during function pointer analysis: " << *Ty << "\n";
    }
}

static std::vector<Value *> GetAllValuesThatWouldBeStripped(Value *V)
{
    SmallPtrSet<const Value *, 8> Visited;
    std::vector<Value *> res;
    bool ciclicalTypeDetected = false;
    Visited.insert(V);
    res.push_back(V);
    while (!ciclicalTypeDetected)
    {

        /*
        There are four cases we are interested in
          1. GEPOperator means the pointer can be used to access the same address as a different type
          2. BitCast Instruction is by definition a type change without modifying the data
          3. AddressSpaceCast Instruction moves the pointer to a different address space, and can redefine the pointer type
          4. CallBase Instruction can be interpreted as the Value being of the function return type
        */
        if (GEPOperator *GEP = dyn_cast<GEPOperator>(V))
        {
            if (!GEP->hasAllZeroIndices())
                // In this case a different memory address is accessed
                // so we cannot handle it as just a type cast
                return res;
            // retrieve the "base" pointer
            V = GEP->getPointerOperand();
        }
        else if (Operator::getOpcode(V) == Instruction::BitCast)
        {
            // first operand is the original type
            Value *NewV = cast<Operator>(V)->getOperand(0);
            if (!NewV->getType()->isPointerTy())
            {
                res.push_back(NewV);
                return res;
            }
            V = NewV;
        }
        else if (Operator::getOpcode(V) == Instruction::AddrSpaceCast)
        {
            // first operand is the original pointer type
            V = cast<Operator>(V)->getOperand(0);
        }
        else if (CallBase *Call = dyn_cast<CallBase>(V))
        {
            if (Value *RV = Call->getReturnedArgOperand())
            {
                V = RV;
            }
            else
            {
                return res;
            }
        }
        else
        {
            // There are no other types we want to handle, this is the end.
            return res;
        }

        assert(V->getType()->isPointerTy() && "Unexpected operand type!");
        res.push_back(V);
        ciclicalTypeDetected = !(Visited.insert(V).second);
    }
    return res;
}

static Value *getFunctionPointerUsingOffsetChain(Value *V, const std::vector<int> &offsetChainToPointer, DebugLoc loc);

static Value *gFPUOC_Struct(Value *V, const std::vector<int> &offsetChainToPointer, Type *Ty, DebugLoc loc)
{
    V = V->stripPointerCastsAndAliases();
    StructType *ST = dyn_cast<StructType>(Ty);
    int offset = offsetChainToPointer[0];
    Value *GEP = GetElementPtrInst::CreateInBounds(ST, V,
                                                   {ConstantInt::get(Type::getInt32Ty(V->getContext()), 0),
                                                    ConstantInt::get(Type::getInt32Ty(V->getContext()), offset)});
    std::vector<int> remaining = std::vector<int>(offsetChainToPointer.begin() + 1, offsetChainToPointer.end());
    errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Accessed index: " << offset << " from value: " << *V << " of type: " << *Ty << " and obtained GEP: " << *GEP << RESET << "\n";
    return getFunctionPointerUsingOffsetChain(GEP, remaining, loc);
}

static Value *gFPUOC_Pointer_DEBUG(Value *V, const std::vector<int> &offsetChainToPointer, Type *Ty, DebugLoc loc)
{
    return getFunctionPointerUsingOffsetChain(V->stripPointerCastsAndAliases(), offsetChainToPointer, loc);
}

static Value *gFPUOC_Pointer(Value *V, const std::vector<int> &offsetChainToPointer, Type *Ty, DebugLoc loc)
{
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V))
    {
        Value *BasePtr = GEP->getPointerOperand();
        errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Transformed value: " << *V << " of type: " << *Ty << " to GEP operator: " << *GEP << " and obtained base pointer: " << *BasePtr << RESET << "\n";
        return getFunctionPointerUsingOffsetChain(BasePtr, offsetChainToPointer, loc);
    }
    else
    {
        // TODO: be able to handle this
        errs() << RED << "[WARNING - LINE " << loc.getLine() << "] Cannot handle non GEP operator on value: " << *V << " of pointer type: " << *Ty << ".\n\tRemaining offsets: ";
        for (int offset : offsetChainToPointer)
        {
            errs() << offset << " ";
        }
        errs() << RESET << "\n";
        return nullptr;
    }
}

static Value *getFunctionPointerUsingOffsetChain(Value *V, const std::vector<int> &offsetChainToPointer, DebugLoc loc)
{
    Type *Ty = V->getType();
    errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Received value: " << *V << " of type: " << *Ty << " in getFunctionPointerUsingOffsetChain" << RESET << "\n";

    if (!(Ty->isPointerTy() || Ty->isStructTy()))
    {
        assert(offsetChainToPointer.size() == 0 && "Offset chain should be empty for non struct/pointer types");
        return V;
    }
    else if (Ty->isPointerTy())
    {
        return gFPUOC_Pointer_DEBUG(V, offsetChainToPointer, Ty, loc);
    }
    else if (Ty->isStructTy())
    {
        return gFPUOC_Struct(V, offsetChainToPointer, Ty, loc);
    }
    else
    {
        errs() << YELLOW << "[WARNING] Unexpected type during function pointer analysis: " << *Ty << RESET << "\n";
        return nullptr;
    }
}


/*
This broke after changing the return type of HandleTypeRecursive.

static std::pair<bool, std::vector<retType>> offsetsToPointersForAllValues(std::vector<Value *> possibleStoredValues)
{
    bool any = false;
    std::vector<retType> res = {};
    for (int i = 0; i < possibleStoredValues.size(); i++)
    {
        Type *StoredType = possibleStoredValues[i]->getType();
        retType partial_res = HandleTypeRecursive(StoredType);
        any = any || partial_res.first;
        res.push_back(partial_res);
    }
    return std::make_pair(any, res);
}
*/

static handleTypeRetType theOneOffsetToPointer(std::vector<Value *> possibleStoredValues, DebugLoc loc)
{
    handleTypeRetType negative_res = std::make_pair(false, vectorTypesOffsets{});
    int any = 0;
    std::vector<handleTypeRetType> res = {};
    std::vector<int> positiveResultIdxs = {};

    for (int i = 0; i < possibleStoredValues.size(); i++)
    {
        Type *StoredType = possibleStoredValues[i]->getType();
        handleTypeRetType partial_res = HandleTypeRecursive(StoredType);

        if (partial_res.first)
        {
            vectorTypesOffsets partial_vector_res = partial_res.second;

            errs() << YELLOW << "[INFO - LLINE " << loc.getLine() << "] Value " << *possibleStoredValues[i] << " of type: " << *StoredType << " can reach function pointer:\n";
            ShowTypesOffsets(partial_res.second);
            errs() << RESET;

            any += 1;
            positiveResultIdxs.push_back(i);
        }
        res.push_back(partial_res);
    }
    // Debug Info
    if (any > 1)
    {
        errs() << RED << "[WARNING] Multiple possible stores leading to function pointers not supported (count = " << any << "). Possible values and offsets:\n";
        for (int i : positiveResultIdxs)
        {
            errs() << "Value: " << *possibleStoredValues[i] << "can reach function pointer:\n";
            ShowTypesOffsets(res[i].second);
        }
        return negative_res;
        // End Debug Info
    }
    else if (any == 1)
    {
        // assume positiveResultIdxs.size() == 1.
        return std::make_pair(true, res[positiveResultIdxs[0]].second);
    }
    else
    {
        return negative_res;
    }
}

static void ShowResults(retType res, Type *ty, Value *memAddress, Value *storedValue, DebugLoc loc, const char *color)
{
    errs() << color << "[LINE " << loc.getLine() << "]" << RESET << " Store of type: " << *ty << " at address: " << *memAddress << "\n";

    errs() << color;
    for (int i = 0; i < res.second.size(); i++)
    {
        std::vector<std::vector<int>> offsetsToFunctionPointers = res.second;
        for (int j = 0; j < offsetsToFunctionPointers[i].size(); j++)
        {
            Value *FunctionPointer = getFunctionPointerUsingOffsetChain(storedValue, offsetsToFunctionPointers[i], loc);
            ShowPointersWithOffsets(offsetsToFunctionPointers[i], storedValue, FunctionPointer, loc);
        }
        errs() << "\n";
    }
    errs() << RESET;
}

/*Unused, for debugging purposes*/
static void printTheOneIWant(StoreInst *SI)
{
    Value *StoredValue = SI->getValueOperand();
    Value *StoredLocation = SI->getPointerOperand();
    Type *StoredType = StoredValue->getType();
    if (DebugLoc loc = SI->getDebugLoc())
    {
        if (loc.getLine() == 58)
        {
            Value *strippedValue = StoredValue->stripPointerCastsAndAliases();
            Type *strippedType = strippedValue->getType();
            errs() << "[LINE " << loc.getLine() << "] Store of type: " << *StoredType << " at: " << *StoredLocation << " of value: " << *StoredValue << "\n";
            errs() << "[LINE " << loc.getLine() << "] Store of type: " << *strippedType << " at: " << *StoredLocation << "\n";
        }
    }
}

// This is Debug
// wtf
static void TheDebugOne(StoreInst *SI)
{
    Value *origValue = SI->getValueOperand();
    Value *StoredLocation = SI->getPointerOperand();
    if (DebugLoc loc = SI->getDebugLoc())
    {
        std::vector<Value *> possible_stored_values = GetAllValuesThatWouldBeStripped(origValue);
        retType one_good = theOneOffsetToPointer(possible_stored_values, loc);
        if (one_good.first)
        {
            Value *StoredValue = origValue->stripPointerCastsAndAliases();
            Type *StoredType = StoredValue->getType();
            for (int i = 0; i < one_good.second.size(); i++)
            {
                Value *FunctionPointer = getFunctionPointerUsingOffsetChain(StoredValue, one_good.second[i], loc);
                ShowPointersWithOffsets(one_good.second[i], StoredValue, FunctionPointer, loc);
            }
        }
    }
}

static void PrintFunctionPointerStores(StoreInst *SI)
{
    Value *origValue = SI->getValueOperand();
    Value *StoredLocation = SI->getPointerOperand();
    if (DebugLoc loc = SI->getDebugLoc())
    {
        std::vector<Value *> possible_stored_values = GetAllValuesThatWouldBeStripped(origValue);

        std::pair<bool, std::vector<retType>> res_for_each = offsetsToPointersForAllValues(possible_stored_values);

        if (res_for_each.first)
        {
            // Debug:
            auto last_idx = res_for_each.second.size() - 1;
            Value *StoredValue = possible_stored_values[last_idx];
            Type *StoredType = StoredValue->getType();
            retType offsetsToFunctionPointers = res_for_each.second[last_idx];
            // What am I doing?
            const char *color = COLORS[0];
            ShowResults(offsetsToFunctionPointers, StoredType, StoredLocation, StoredValue, loc, color);
        }
    }
}

PreservedAnalyses FindStoreValuesPass::run(Module &M, ModuleAnalysisManager &MAM)
{
    for (auto &F : M)
    {
        for (auto &BB : F)
        {
            for (auto &I : BB)
            {
                if (StoreInst *SI = dyn_cast<StoreInst>(&I))
                    TheDebugOne(SI);
            }
        }
    }

    return PreservedAnalyses::all();
}

PassPluginLibraryInfo getFindStoreValuesPassPluginInfo()
{
    return {LLVM_PLUGIN_API_VERSION, "FindStoreValuesPass", LLVM_VERSION_STRING,
            [](PassBuilder &PB)
            {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, ModulePassManager &MPM,
                       ArrayRef<PassBuilder::PipelineElement>)
                    {
                        // This is the flag you will use with `opt`, e.g., -passes=playground-pass
                        if (Name == "find-store-values-pass")
                        {
                            MPM.addPass(FindStoreValuesPass());
                            return true;
                        }
                        return false;
                    });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo()
{
    return getFindStoreValuesPassPluginInfo();
}
