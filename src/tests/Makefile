CC := clang
LLC := ../llvm/build/bin/llc
CPOINTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libCheckPointPass.so
UNOPTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libUnoptimizedCopyPass.so
LIVEVARPASS := -Xclang -load -Xclang ../passes/build/function_passes/libLiveVariablesPass.so
MARKPASS := -Xclang -load -Xclang ./passes/build/basic_block_passes/libMarkUnoptimizedPass.so
PASSFLAGS := $(UNOPTPASS) $(CPOINTPASS) $(LIVEVARPASS)
STMAP_CHECKER_DIR := ../stackmap_checker/
OBJ_NAMES := utils.o stmap.o jump.o guard.o call_stack_state.o
OBJS := $(foreach obj, $(OBJ_NAMES), $(STMAP_CHECKER_DIR)$(obj))
PREFIX := trace
EXECUTABLES := $(basename $(wildcard trace*.c))
TARGET_OBJS := $(foreach bin, $(EXECUTABLES), $(bin).o)

.PHONY: all clean stackmap_checker

all: stackmap_checker $(EXECUTABLES)

stackmap_checker:
	cd ../stackmap_checker/ && $(MAKE)

.SECONDEXPANSION:
$(EXECUTABLES): $$@.o
	$(CC) -o $@ $(OBJS) $@.o -O3 -lunwind

bytecode: $(PREFIX)%.c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

$(TARGET_OBJS): $$(basename $$@).ll
	$(LLC) -filetype=obj $<
	$(LLC) -filetype=obj $<

trace_marked.ll: trace_marked.c
	$(CC) $(PASSFLAGS) $(MARKPASS) -S -emit-llvm $< -O3

%.ll: %.c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

clean:
	rm -f $(EXECUTABLES) *.o *.ll /tmp/__stack_resizer_*
