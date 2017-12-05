#include <stdio.h>

void unopt() {
    int x = 2;
    printf("unopt: %d\n", x);
}

void trace() {
    int x = 1;
    printf("trace %d\n", x);
}

int main(int argc, char **argv) {
    trace();
    return 0;
}
