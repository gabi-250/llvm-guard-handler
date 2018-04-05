#include <stdio.h>


char more_indirection()
{
    char c = 'x';
    return c;
}

int main(int argc, char **argv)
{
    char x = more_indirection();
    printf("%c\n", x);
    return 0;
}
