#include "FindStoreValuesPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/IRBuilder.h"
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

static void showTypeAndOffsetChain(Types typeChain, Offsets offsetChain)
{
    errs() << "{\n";
    for (int j = 0; j < typeChain.size(); j++)
    {
        errs() << std::string('\t', j) << "Type: " << *typeChain[j] << " Offset: " << offsetChain[j] << "\n";
    }
    errs() << "}\n";
}

static void ShowTypesOffsets(vectorTypesOffsets typesOffsets)
{
    errs() << "{\n";
    for (int i = 0; i < typesOffsets.size(); i++)
    {
        showTypeAndOffsetChain(typesOffsets[i].first, typesOffsets[i].second);
    }
    errs() << "}\n";
}

static void ShowPointersWithOffsets(Offsets offsetChain, Types typeChain, Value *storedValue, Value *FunctionPointer)
{
    if (FunctionPointer)
    {
        errs() << "Function pointer value:" << *FunctionPointer << " at chain:\n";
        showTypeAndOffsetChain(typeChain, offsetChain);
    }
    else
    {
        errs() << "\t Failed to retrieve function pointer value from " << *storedValue << " at chain:\n";
        showTypeAndOffsetChain(typeChain, offsetChain);
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
            errs() << YELLOW;
            errs() << "[INFO] Struct type: " << *OriginalStruct << " can reach function pointer through element " << i << " of type: " << *ElemType << "\n";
            errs() << "Current chains (size=" << this_types_offsets.size() << "):\n";
            ShowTypesOffsets(this_types_offsets);
            if (this_types_offsets.size() == 0)
            {
                this_types_offsets.push_back(
                    std::make_pair(Types{}, Offsets{}));
            }
            for (int j = 0; j < this_types_offsets.size(); j++)
            {
                Types this_types_j = this_types_offsets[j].first;
                Offsets this_offsets_j = this_types_offsets[j].second;
                errs() << "Internal chain:\n";
                showTypeAndOffsetChain(this_types_j, this_offsets_j);
                this_types_j.insert(this_types_j.begin(), OriginalStruct);
                this_offsets_j.insert(this_offsets_j.begin(), i);
                errs() << "Updated internal chain:\n";
                showTypeAndOffsetChain(this_types_j, this_offsets_j);
                this_types_offsets[j] = std::make_pair(this_types_j, this_offsets_j);
            }
            all_vector_res.insert(all_vector_res.end(), this_types_offsets.begin(), this_types_offsets.end());
            errs() << "Final chains (size=" << all_vector_res.size() << "):\n";
            ShowTypesOffsets(all_vector_res);
            errs() << RESET;
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

static Instruction *FindSafeInsertionPoint(Value *V)
{
    if (Instruction *Inst = dyn_cast<Instruction>(V))
    {
        // Special case: If V is a Phi node, we cannot insert immediately after it.
        // We must insert after ALL Phi nodes in that basic block.
        if (isa<PHINode>(Inst))
        {
            return Inst->getParent()->getFirstNonPHI();
        }
        // Normal instruction: insert right after it
        return Inst->getNextNode();
    }
    else if (Argument *Arg = dyn_cast<Argument>(V))
    {
        // Arguments are available immediately. Insert at the start of the function.
        return &*Arg->getParent()->getEntryBlock().getFirstInsertionPt();
    }

    // If it's a GlobalVariable or something else, it's safer to return nullptr
    // and handle it as a floating instruction, or pass the specific function context.
    return nullptr;
}

static Value *getFunctionPointerUsingOffsetChain(Value *V, Offsets offsetChain, Types typeChain, DebugLoc loc);

static Value *gFPUOC_Struct(Value *V, Offsets offsetChain, Types typeChain, DebugLoc loc)
{
    errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Received value: " << *V << " of type: " << *V->getType() << " in gFPUOC_Struct. Using chain:\n";
    ShowTypesOffsets({std::make_pair(typeChain, offsetChain)});
    errs() << RESET;

    V = V->stripPointerCastsAndAliases();

    int offset = offsetChain[0];
    Type *structType = typeChain[0];

    ArrayRef<Value *> arrayParam = {ConstantInt::get(Type::getInt32Ty(V->getContext()), 0),
                                    ConstantInt::get(Type::getInt32Ty(V->getContext()), offset)};

    IRBuilder<> Builder(FindSafeInsertionPoint(V));
    Builder.SetCurrentDebugLocation(loc);

    unsigned AddrSpace = V->getType()->getPointerAddressSpace();
    PointerType *StructPtrTy = PointerType::get(structType, AddrSpace);
    Value *Cast = Builder.CreatePointerCast(V, StructPtrTy);

    errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Will use Type: " << *structType << " and offset: " << offset << " in gFPUOC_Struct\n"
           << RESET;
    Value *GEP = Builder.CreateInBoundsGEP(structType, Cast, arrayParam);

    errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Accessed value: " << *V << " of type: " << *V->getType() << "using type: " << *structType << " with offset: " << offset << " and obtained GEP: " << *GEP << RESET << "\n";

    Offsets remaining_offsets = Offsets(offsetChain.begin() + 1, offsetChain.end());
    Types remaining_types = Types(typeChain.begin() + 1, typeChain.end());
    return getFunctionPointerUsingOffsetChain(GEP, remaining_offsets, remaining_types, loc);
}

static Value *gFPUOC_Pointer_DEBUG(Value *V, Offsets offsetChain, Types typeChain, DebugLoc loc)
{
    return gFPUOC_Struct(V->stripPointerCastsAndAliases(), offsetChain, typeChain, loc);
}

static Value *gFPUOC_Pointer(Value *V, Offsets offsetChain, Types typeChain, DebugLoc loc)
{
    Type *Ty = V->getType();
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V))
    {
        Value *BasePtr = GEP->getPointerOperand();
        errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Transformed value: " << *V << " of type: " << *Ty << " to GEP operator: " << *GEP << " and obtained base pointer: " << *BasePtr << RESET << "\n";
        return getFunctionPointerUsingOffsetChain(BasePtr, offsetChain, typeChain, loc);
    }
    else
    {
        // TODO: be able to handle this
        errs() << RED << "[WARNING - LINE " << loc.getLine() << "] Cannot handle non GEP operator on value: " << *V << " of pointer type: " << *Ty << ".\n\tRemaining offsets: ";
        ShowTypesOffsets({std::make_pair(typeChain, offsetChain)});
        errs() << RESET;
        return nullptr;
    }
}

static Value *getFunctionPointerUsingOffsetChain(Value *V, Offsets offsetChain, Types typeChain, DebugLoc loc)
{
    Type *Ty = V->getType();
    errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Received value: " << *V << " of type: " << *Ty << " in getFunctionPointerUsingOffsetChain\n. Using chain:\n";
    ShowTypesOffsets({std::make_pair(typeChain, offsetChain)});
    errs() << RESET;

    if (!(Ty->isPointerTy() || Ty->isStructTy()))
    {
        assert(offsetChain.size() == 0 && "Offset chain should be empty for non struct/pointer types");
        return V;
    }
    else if (Ty->isPointerTy())
    {
        return gFPUOC_Pointer_DEBUG(V, offsetChain, typeChain, loc);
    }
    else if (Ty->isStructTy())
    {
        return gFPUOC_Struct(V, offsetChain, typeChain, loc);
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

            errs() << YELLOW << "[INFO - LINE " << loc.getLine() << "] Value " << *possibleStoredValues[i] << " of type: " << *StoredType << " can reach function pointer:\n";
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

/*
This broke after changing the function signature of ShowPointersWithOffsets.

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
*/

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
        handleTypeRetType one_good = theOneOffsetToPointer(possible_stored_values, loc);
        if (one_good.first)
        {
            vectorTypesOffsets typesOffsets = one_good.second;
            Value *StoredValue = origValue->stripPointerCastsAndAliases();
            for (int i = 0; i < one_good.second.size(); i++)
            {
                Types typeChain = typesOffsets[i].first;
                Offsets offsetChain = typesOffsets[i].second;
                Value *FunctionPointer = getFunctionPointerUsingOffsetChain(StoredValue, offsetChain, typeChain, loc);
                ShowPointersWithOffsets(offsetChain, typeChain, StoredValue, FunctionPointer);
            }
        }
    }
}

/*
This broke after changing the return type of HandleTypeRecursive.

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
*/

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
