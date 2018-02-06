CC = clang
LLC = ../llvm/build/bin/llc
CFLAGS := -g $(shell llvm-config --cflags) -Wno-language-extension-token
LDFLAGS := $(shell llvm-config  --cflags --ldflags --libs all --system-libs) -lstdc++
CPOINTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libCheckPointPass.so
UNOPTPASS := -Xclang -load -Xclang ../passes/build/function_passes/libUnoptimizedCopyPass.so
PASSFLAGS := $(CPOINTPASS) $(UNOPTPASS)
OBJS := utils.o stmap.o jump.o
EXECUTABLE := trace

.PHONY: all clean

all: $(EXECUTABLE)

trace: $(OBJS) $(EXECUTABLE).o
	$(CC) $(PASSFLAGS) -o $@ $(OBJS) $(EXECUTABLE).o -O3

bytecode: $(EXECUTABLE).c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

$(EXECUTABLE).o: $(EXECUTABLE).ll
	$(LLC) -filetype=obj $<
	$(LLC) -filetype=obj $<

%.ll: %.c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

clean:
	rm -f $(EXECUTABLE) *.o *.ll
