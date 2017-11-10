CC = clang
CXX = clang++

CFLAGS := -g $(shell llvm-config --cxxflags) -std=c++11
LDFLAGS := $(shell llvm-config  --cxxflags --ldflags --libs all --system-libs) -lstdc++

.PHONY: all clean

all: inspect odd.bc

%.bc: %.c
	$(CC) -emit-llvm $< -c -o $@
%: %.cpp
	$(CXX) $< -o $@ $(LDFLAGS)
clean:
	rm -f *.bc *.o *.ll *.s odd inspect
