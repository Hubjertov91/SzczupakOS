#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define COLOR_DIR   "\033[1;34m"
#define COLOR_RESET "\033[0m"

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/";
    
    long fd = sys_opendir(path);
    if (fd < 0) {
        printf("ls: cannot access '%s': No such directory\n", path);
        return 1;
    }
    
    struct dirent entry;
    int count = 0;
    
    while (sys_readdir(fd, &entry) > 0) {
        if (entry.type == 1) {
            printf(COLOR_DIR "%s/" COLOR_RESET "\n", entry.name);
        } else {
            printf("%s\n", entry.name);
        }
        count++;
    }
    
    if (count == 0) {
        printf("(empty directory)\n");
    }
    
    sys_closedir(fd);
    return 0;
}