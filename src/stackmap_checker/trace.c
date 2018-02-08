#include <stdlib.h>
#include <stdio.h>

int more_indirection()
{
    return 3;
}

int get_number(int level)
{
    if (level < 2) {
        putchar(level + '0');
        putchar('\n');
        return get_number(level + 1);
    } else {
        char one = '1';
        char two = 2 + '0';
        int x = more_indirection();
        putchar(one);
        putchar(two);
        putchar('\n');
        return x;
    }
}

void trace()
{
    char four = '4';
    int y = 155;
    int x = get_number(0);
    putchar(x +'0');
    putchar('\n');
    putchar(four);
    putchar('\n');

    putchar(y % 10 + '0');
    putchar('\n');
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
