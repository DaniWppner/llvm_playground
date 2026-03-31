# LLVM Playground

# All of this has to be done inside the docker container

## To compile `StoreFunction.c`


```bash
cd src/TestPrograms
# will create executable
clang -g StoreFunction.c -o StoreFunction.o
# will create analyzable intermediate representation
clang -g -S -emit-llvm StoreFunction.c -o StoreFunction.ll
```


## To build the LLVM pass `PlaygroundPass`


```bash
mkdir src/build
cd src/build
cmake ..
make
```

This creates a pass plugin shared library `src/build/PlaygroundPass/playground_pass.so`.

## To run the pass on LLVM IR with `opt`

```bash
cd src
opt -load-pass-plugin=/home/src/build/PlaygroundPass/playground_pass.so -passes=playground-pass -S /home/src/TestPrograms/StoreFunction.ll -o out.ll
```

## If you want to use `DynamicStructs.c` or the pass `FindStoreValuesPass` follow similar instructions
