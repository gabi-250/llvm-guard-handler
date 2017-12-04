CC = clang

CFLAGS := -g $(shell llvm-config --cflags)
LDFLAGS := $(shell llvm-config  --cflags --ldflags --libs all --system-libs) -lstdc++

OBJS := runtime.o stmap.o
EXECUTABLE := runtime

.PHONY: all clean

all: $(EXECUTABLE)

runtime: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.h

clean:
	rm -f $(EXECUTABLE) *.o
