#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include "stdint.h"

#define SYSCALL_EXIT    0
#define SYSCALL_WRITE   1
#define SYSCALL_READ    2
#define SYSCALL_GETPID  3
#define SYSCALL_GETTIME 4
#define SYSCALL_SLEEP   5
#define SYSCALL_CLEAR   6
#define SYSCALL_SYSINFO 7
#define SYSCALL_FORK    8
#define SYSCALL_EXEC    9
#define SYSCALL_FB_PUTPIXEL 10
#define SYSCALL_FB_CLEAR    11
#define SYSCALL_FB_RECT     12
#define SYSCALL_FB_INFO     13
#define SYSCALL_FB_PUTCHAR  14
#define SYSCALL_FB_PUTCHAR_PSF 15   
#define SYSCALL_LISTDIR     16


struct sysinfo {
    uint64_t uptime;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t nr_processes;
};

struct fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

bool syscall_init(void);

#endif
