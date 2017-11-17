#include <stdio.h>

void f(int x) {
    printf("%d\n", x);
}

int main(int argc, char **argv) {
    f(argc);
    if (argc % 2 == 0) {
        printf("Even!\n");
    } else {
        printf("Odd!\n");
    }
    return 0;
}
