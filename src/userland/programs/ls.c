#include "syscall.h"

int main() {
    for (int i = 0; i < 10; i++)
        sys_write("x\n", 2);
    
    sys_exit(0);
}