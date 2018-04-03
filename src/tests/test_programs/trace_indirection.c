#include <stdio.h>

int more_indirection()
{
    int x = 3;
    return x;
}

int more_indirection2()
{
    return more_indirection();
}

void trace()
{
    int x = more_indirection2();
    printf("x = %d\n", x);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
