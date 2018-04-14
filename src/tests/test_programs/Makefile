CC := clang
ROOT_DIR:= ../../
PASS_DIR:= $(ROOT_DIR)passes/build/
LLC :=$(ROOT_DIR)llvm/build/bin/llc
CPOINTPASS := -Xclang -load -Xclang $(PASS_DIR)function_passes/libCheckPointPass.so
UNOPTPASS := -Xclang -load -Xclang $(PASS_DIR)function_passes/libUnoptimizedCopyPass.so
LIVEVARPASS := -Xclang -load -Xclang $(PASS_DIR)function_passes/libLiveVariablesPass.so
BARRIERPASS := -Xclang -load -Xclang $(PASS_DIR)basic_block_passes/libBarrierPass.so
PASSFLAGS := $(CPOINTPASS) $(LIVEVARPASS) $(UNOPTPASS) $(BARRIERPASS)
STMAP_CHECKER_DIR := $(ROOT_DIR)stackmap_checker/
OBJ_NAMES := utils.o stmap.o jump.o guard.o call_stack_state.o
OBJS := $(foreach obj, $(OBJ_NAMES), $(STMAP_CHECKER_DIR)$(obj))
EXECUTABLES := $(basename $(wildcard trace*.c))
CLANG_COMPILED := $(foreach obj, $(EXECUTABLES), $(obj)_clang_)
TARGET_OBJS := $(foreach bin, $(EXECUTABLES), $(bin).o)

.PHONY: all clean stackmap_checker

all: stackmap_checker $(EXECUTABLES)

stackmap_checker:
	cd $(STMAP_CHECKER_DIR) && $(MAKE)

.SECONDEXPANSION:
$(EXECUTABLES): $$@.o
	$(CC) -o $@ $(OBJS) $@.o -O3 -lunwind

$(TARGET_OBJS): $$(basename $$@).ll
	$(LLC) -filetype=obj $<
	$(LLC) -filetype=obj $<
	rm .stack_resizer_*

%.ll: %.c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

clean:
	rm -f $(EXECUTABLES) $(CLANG_COMPILED) *.o *.ll /tmp/__stack_resizer_*
