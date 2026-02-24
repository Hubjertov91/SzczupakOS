#include <stdio.h>
#include <syscall.h>

#define LS_BUF_SIZE 2048

int main(void) {
    char output[LS_BUF_SIZE];
    long n = sys_listdir("/", output, LS_BUF_SIZE - 1);
    if (n < 0) {
        printf("ls: failed to list directory\n");
        sys_exit(1);
        return 1;
    }

    if (n > 0) {
        sys_write(output, n);
    }

    sys_exit(0);
    return 0;
}
