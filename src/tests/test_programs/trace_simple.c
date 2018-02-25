#include <stdio.h>

int more_indirection()
{
    int x = 3;
    return x;
}

void trace()
{
    int x = more_indirection();
    printf("x = %d\n", x);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
