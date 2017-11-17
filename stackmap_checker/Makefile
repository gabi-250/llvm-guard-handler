CC = clang

CFLAGS := -g $(shell llvm-config --cflags)
LDFLAGS := $(shell llvm-config  --cflags --ldflags --libs all --system-libs) -lstdc++

.PHONY: all clean

all: inspect

%: %.c
	$(CC) $< -o $@ $(LDFLAGS)
clean:
	rm -f inspect
