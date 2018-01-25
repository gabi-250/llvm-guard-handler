# Building the passes

To build the two IR function passes, run:

```
./build_pass
```

To run one of the passes on `<filename>.c`:

```
clang -Xclang -load -Xclang build/<pass_name>.so -o <filename> <filename>.c
```
