CC = clang

CFLAGS := -g $(shell llvm-config --cflags) -Wno-language-extension-token
LDFLAGS := $(shell llvm-config  --cflags --ldflags --libs all --system-libs) -lstdc++
CPOINTPASS := -Xclang -load -Xclang ../build/function_passes/libCheckPointPass.so
UNOPTPASS := -Xclang -load -Xclang ../build/function_passes/libUnoptimizedCopyPass.so
PASSFLAGS := $(CPOINTPASS) $(UNOPTPASS)
OBJS := utils.o stmap.o
EXECUTABLE := trace

.PHONY: all clean

all: $(EXECUTABLE)

trace: $(OBJS) trace.o
	$(CC) $(PASSFLAGS) -o $@ $@.c $(OBJS) -O3

bytecode: $(EXECUTABLE).c
	$(CC) $(PASSFLAGS) -S -emit-llvm $< -O3

%.o: %.h

clean:
	rm -f $(EXECUTABLE) *.o *.ll
