# llvm-playground
Experimenting with LLVM

To compile the pass, run:

```
mkdir -p build && cd build && cmake .. && make; cd ..
```

To test the pass, link it with one of the example programs:

```
clang -Xclang -load  -Xclang build/checkpoint/libCheckPointPass.so -S -emit-llvm examples/odd.c
```

This will generate an IR file that contains a stack map call before each jump
instruction.
