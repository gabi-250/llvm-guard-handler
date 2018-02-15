CC = clang
LLC = ../llvm/build/bin/llc
CPOINTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libCheckPointPass.so
UNOPTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libUnoptimizedCopyPass.so
PASSFLAGS := $(CPOINTPASS) $(UNOPTPASS)
OBJS := utils.o stmap.o jump.o guard.o call_stack_state.o
EXECUTABLE := trace

.PHONY: all clean

all: $(EXECUTABLE)

trace: $(OBJS) $(EXECUTABLE).o
	$(CC) $(PASSFLAGS) -o $@ $(OBJS) $(EXECUTABLE).o -O3 -lunwind

bytecode: $(EXECUTABLE).c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

$(EXECUTABLE).o: $(EXECUTABLE).ll
	$(LLC) -filetype=obj $<
	$(LLC) -filetype=obj $<

%.ll: %.c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

clean:
	rm -f $(EXECUTABLE) *.o *.ll /tmp/__stack_resizer_*
