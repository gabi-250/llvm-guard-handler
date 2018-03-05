#include <stdio.h>

typedef struct LargeStruct {
    int a;
    int b;
    long c;
} large_struct_t;

int more_indirection()
{
    return 100;
}

void trace()
{
    large_struct_t y;
    y.a = 1;
    y.b = 1;
    y.c = 10000000000;
    int x = more_indirection();
    printf("x = %d\n", x);
    printf("y = %d\n", y.a);
    printf("z = %ld\n", y.c);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
