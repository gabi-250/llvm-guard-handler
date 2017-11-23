CC = clang

CFLAGS := -g $(shell llvm-config --cflags)
LDFLAGS := $(shell llvm-config  --cflags --ldflags --libs all --system-libs) -lstdc++

OBJS := inspect.o stmap.o
EXECUTABLE := inspect

.PHONY: all clean

all: $(EXECUTABLE)

inspect: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.h

clean:
	rm -f $(EXECUTABLE) *.o
