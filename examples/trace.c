#include <stdio.h>

int gf_addr2 = 0;

void trace() {
    int x = 1;
    int y = gf_addr2 + 1;
    printf("trace %d\n", x);
}

void unopt() {
    int x = 2;
    printf("unopt: %d\n", x);
}

int main(int argc, char **argv) {
    trace();
    return 0;
}
