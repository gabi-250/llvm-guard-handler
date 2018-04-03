#include <stdio.h>

int more_indirection()
{
    return 100;
}

void trace()
{
    long double y = 1324.34234;
    int x = more_indirection();
    printf("x = %d\n", x);
    printf("y = %Lf\n", y);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
