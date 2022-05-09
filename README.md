# llvm-guard-handler

## Dependencies

The following libraries are required for the native guard failure handler to
run:
* LLVM 5.0.0
* [libunwind 1.1-4.11](http://www.nongnu.org/libunwind/)
* [elfutils-0.168](http://www.linuxfromscratch.org/blfs/view/8.0/general/elfutils.html)

In addition, Clang 5.0.0 must be installed.

To view the execution, it is necessary to install `gdb` or a similar debugger. It
is recommended to use `cgdb` as an interface to `gdb`.

## Using `llvm-guard-handler`

Clone the project using
`git clone --recursive https://github.com/gabi-250/llvm-guard-handler`.
This will also clone the llvm submodule, so it may take a few minutes to
complete.

### Compiling `llc`

The native guard failure handler requires a custom version of `llc` to run. For
this reason, LLVM was included as a submodule of the project. To compile `llc`, cd
into the folder in which you cloned the repository, and run:

```
cd src/llvm/
mkdir build
cd build
cmake ..
make llc
```

This will compile `llc`. Note that this may be a slow process.

### Compiling the preprocessing passes

To compile the preprocessing passes, ensure the current directory is the root of
the project and run:

```
cd src/passes
./build_pass
```

### Running the test programs

To run the test programs, ensure the current directory is the root of the project
and run:

```
cd src/tests/test_control_flow/passes/
./build_pass
cd ../../../
pytest tests
```

### Using `gdb` to examine the execution

First, compile the test programs by running:
```
cd src/tests/test_programs
make
```

Then, run one of the resulting binaries in `gdb`. If, for example, you want to
test the trace binary, run `gdb trace`.

In `gdb`, run the following commands to display the assembly instructions of
the program, and the register state:
```
(gdb) layout asm
(gdb) layout regs
```

To set break points at the two recovery routines execute the following com-
mands:
```
(gdb) break restore_inlined
(gdb) break jmp_to_addr
```

This will pause the execution before resuming execution in the unoptimised
version of `trace`.

To run the program in `gdb`, type `run`. Then, to execute the next instruction,
type `ni`. To view the state of the program, as the execution is being resumed
in its unoptimised version, continue typing `ni`, until the execution completes.
Alternatively, set multiple break points in the program, and use `continue` to
run the program until the next breakpoint is reached.
