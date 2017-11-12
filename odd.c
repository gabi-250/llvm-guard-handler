#include <stdio.h>

void f(int x) {
   if (x < 3) {
      printf("%d\n", x);
   } else {
      printf("x >= 3\n");
   }
}

int main(int argc, char **argv) {
   f(argc);
   return 0;
}
