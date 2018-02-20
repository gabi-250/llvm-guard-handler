CC := clang
LLC := ../llvm/build/bin/llc
CPOINTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libCheckPointPass.so
UNOPTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libUnoptimizedCopyPass.so
PASSFLAGS := $(CPOINTPASS) $(UNOPTPASS)
OBJS := utils.o stmap.o jump.o guard.o call_stack_state.o
PREFIX := trace
EXECUTABLES := $(basename $(wildcard trace*.c))
TARGET_OBJS := $(foreach bin, $(EXECUTABLES), $(bin).o)

.PHONY: all clean bytecode

all: $(OBJS)

traces: $(EXECUTABLES)

.SECONDEXPANSION:
$(EXECUTABLES): $(OBJS) $$@.o
	$(CC) -o $@ $(OBJS) $@.o -O3 -lunwind

bytecode: $(PREFIX)%.c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

$(TARGET_OBJS): $$(basename $$@).ll
	$(LLC) -filetype=obj $<
	$(LLC) -filetype=obj $<

%.ll: %.c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

clean:
	rm -f $(EXECUTABLES) *.o *.ll /tmp/__stack_resizer_*
