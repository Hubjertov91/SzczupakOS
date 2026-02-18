#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_GETPID  3
#define SYS_GETTIME 4
#define SYS_SLEEP   5
#define SYS_CLEAR   6
#define SYS_SYSINFO 7
#define SYS_FORK    8
#define SYS_EXEC    9
#define SYS_KILL    10
#define SYS_MODULE_LOAD  11
#define SYS_MODULE_UNLOAD 12
#define SYS_FB_PUTPIXEL 13
#define SYS_FB_CLEAR    14
#define SYS_FB_RECT     15
#define SYS_FB_INFO     16
#define SYS_FB_PUTCHAR  17
#define SYS_FB_PUTCHAR_PSF 18

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

long syscall0(long n);
long syscall1(long n, long a1);
long syscall2(long n, long a1, long a2);
long syscall3(long n, long a1, long a2, long a3);
long syscall4(long n, long a1, long a2, long a3, long a4);
long syscall5(long n, long a1, long a2, long a3, long a4, long a5);

void sys_exit(int code);
long sys_write(const char* buf, long len);
long sys_read(char* buf, long len);
long sys_getpid(void);
long sys_gettime(void);
void sys_sleep(long ms);
void sys_clear(void);
long sys_sysinfo(struct sysinfo* info);
long sys_fork(void);
long sys_kill(long pid, long signal);
long sys_module_load(const char* name, const void* data, long size);
long sys_module_unload(const char* name);
long sys_fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
long sys_fb_clear(uint32_t color);
long sys_fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
long sys_fb_info(struct fb_info* info);
long sys_fb_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
long sys_fb_putchar_psf(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);

#endif