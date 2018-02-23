#include <stdlib.h>
#include <stdio.h>

int more_indirection()
{
    return 1;
}

int get_number(int level)
{
    if (level < 2) {
        int x = get_number(level + 1);
        printf("%d\n", x % 10);
        return more_indirection();
    } else {
        return 2;
    }
}

void trace()
{
    int x = get_number(0);
    printf("%d\n", x);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
