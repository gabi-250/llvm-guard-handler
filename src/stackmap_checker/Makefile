CC := clang
CFLAGS := -g
OBJS := utils.o stmap.o jump.o guard.o call_stack_state.o
EXECUTABLES := $(basename $(wildcard trace*.c))

.PHONY: all clean

all: $(OBJS)

clean:
	rm -f $(EXECUTABLES) *.o
