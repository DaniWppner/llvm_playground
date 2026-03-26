#include <stdio.h>
#include <stdlib.h>
#include <time.h>
// This is a program that dynamically assigns sections to a struct, which may contain function pointers.
// This is so when the structs are used it is impossible to know what is actually contained in them.
// 
// It is meant for the purpose of playground analysis with LLVM.


int globals_1 = 0;
float globals_2 = 0.0;

struct FunctionDescGeneric {
    int x;
    int y;
    int (*dispatcher)(struct FunctionDescGeneric*);
    void *data;
};

struct FunctionOneData {
    int c;
    int (*fn)(int, int, int);
};

struct FunctionTwoData {
    double c;
    double d;
    int (*fn)(int, int, double, double);
};

int function_one(int a, int b, int c) {
    globals_1 += a + b - c;
    return 0;
}

int function_two(int a, int b, double c, double d) {
    globals_2 += a + b - d / c;
    return 1;
}

int FunctionOneDispatcher(struct FunctionDescGeneric *fnDesc){
    struct FunctionOneData *args = fnDesc->data;
    return args->fn(fnDesc->x, fnDesc->y, args->c);
}

int FunctionTwoDispatcher(struct FunctionDescGeneric *fnDesc){
    struct FunctionTwoData *args = fnDesc->data;
    return args->fn(fnDesc->x, fnDesc->y, args->c, args->d);
}

void createFunctionOneData(struct FunctionDescGeneric *fnDesc) {
    struct FunctionOneData *fun_data = malloc(sizeof(struct FunctionOneData));
    if (fun_data){
        fun_data->c = -6;
        fun_data->fn = &function_one;
    }
    fnDesc->dispatcher = &FunctionOneDispatcher;
    fnDesc->data = fun_data;
}

void createFunctionTwoData(struct FunctionDescGeneric *fnDesc) {
    struct FunctionTwoData *fun_data = malloc(sizeof(struct FunctionTwoData));
    if (fun_data){
        fun_data->c = 3.14161519;
        fun_data->d = 2.71282818;
        fun_data->fn = &function_two;
    }
    fnDesc->dispatcher = &FunctionTwoDispatcher;
    fnDesc->data = fun_data;
}

// Don't care about the leakage
struct FunctionDescGeneric *getRandomStructOneOrTwo() {
    struct FunctionDescGeneric *fnDesc = malloc(sizeof(struct FunctionDescGeneric));
    if (fnDesc) { fnDesc->x = 4; fnDesc->y = 8;}

    if (time(NULL) % 2 == 0) {
        createFunctionOneData(fnDesc);
    } else{
        createFunctionTwoData(fnDesc);
    }
    return fnDesc;
}

int main(void) {
    struct FunctionDescGeneric *picked = getRandomStructOneOrTwo();
    int result = picked->dispatcher(picked);
    printf("chosen global variable=%d\nvalue of first global variable=%d\nvalue of first global variable=%f\n",result, globals_1, globals_2);
    free(picked);
    return 0;
}