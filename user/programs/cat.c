#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define BUFFER_SIZE 512

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("cat: missing filename\n");
        return 1;
    }
    
    long fd = sys_open(argv[1], 0);
    if (fd < 0) {
        printf("cat: cannot open '%s'\n", argv[1]);
        return 1;
    }
    
    char buffer[BUFFER_SIZE];
    long nread;
    while ((nread = sys_fread(fd, buffer, BUFFER_SIZE)) > 0) {
        for (long i = 0; i < nread; i++) {
            printf("%c", buffer[i]);
        }
    }
    
    sys_close(fd);
    return 0;
}
