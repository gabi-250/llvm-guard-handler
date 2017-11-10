#include <stdio.h>

void f(int x) {
   if (x < 2) {
      printf("%d\n", x);
   } else {
      printf("greater than 0\n");
   }
}

int main(int argc, char **argv) {
   f(argc);
   return 0;
}
