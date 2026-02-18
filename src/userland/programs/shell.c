#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <stddef.h>

int main(void) {
    printf("Shell started\n");
    sys_fb_clear(0x0000FF);
    sys_fb_rect(0, 0, 100, 100, 0x00FF00);

    while (1) {
        sys_sleep(1000);
        printf("tick\n");
    }
    
    return 0;
}
