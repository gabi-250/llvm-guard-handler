#include <stdlib.h>
#include <stdio.h>

int more_indirection()
{
    return 3;
}

int get_number()
{
    int x = more_indirection();
    return x;
}

void trace()
{
    int x = get_number();
    putchar(x +'0');
    putchar('\n');
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
