#include <stdlib.h>
#include <stdio.h>

int more_indirection()
{
    return 3;
}

int get_number(int level)
{
    double dbl = 2.54645;
    if (level < 2) {
        printf("Call %d\n", level);
        return get_number(level + 1);
    } else {
        char one = '1';
        char two = 2 + '0';
        long a_long = 249238493223;
        int x = more_indirection();
        printf("dbl = %lf\n", dbl);
        printf("one = %c\n", one);
        printf("two = %c\n", two);
        printf("a long = %ld\n", a_long);
        printf("x = %d\n", x);
        return x;
    }
}

void trace()
{
    char four = '4';
    int y = 155;
    double k = 8.2345;
    int x = get_number(0);
    printf("x = %d\n", x);
    printf("y = %d\n", y);
    printf("four = %c\n", four);
    printf("k = %lf\n", k);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
