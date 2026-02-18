#include <syscall.h>

long syscall0(long n) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall1(long n, long a1) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

void sys_exit(int code) {
    syscall1(SYS_EXIT, (long)code);
    for(;;);
}

long sys_write(const char* buf, long len) {
    return syscall2(SYS_WRITE, (long)buf, len);
}

long sys_read(char* buf, long len) {
    return syscall2(SYS_READ, (long)buf, len);
}

long sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

long sys_gettime(void) {
    return syscall0(SYS_GETTIME);
}

void sys_sleep(long ms) {
    syscall1(SYS_SLEEP, ms);
}

void sys_clear(void) {
    syscall0(SYS_CLEAR);
}

long sys_sysinfo(struct sysinfo* info) {
    return syscall1(SYS_SYSINFO, (long)info);
}

long sys_fork(void) {
    return syscall0(SYS_FORK);
}

long sys_fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    return syscall3(SYS_FB_PUTPIXEL, x, y, color);
}

long sys_fb_clear(uint32_t color) {
    return syscall1(SYS_FB_CLEAR, color);
}

long sys_fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    return syscall5(SYS_FB_RECT, x, y, w, h, color);
}

long sys_fb_info(struct fb_info* info) {
    return syscall1(SYS_FB_INFO, (long)info);
}

long sys_fb_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    return syscall5(SYS_FB_PUTCHAR, x, y, (long)c, fg, bg);
}

long sys_fb_putchar_psf(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    return syscall5(SYS_FB_PUTCHAR_PSF, x, y, c, fg, bg);
}

long sys_kill(long pid, long signal) {
    return syscall2(SYS_KILL, pid, signal);
}

long sys_module_load(const char* name, const void* data, long size) {
    return syscall3(SYS_MODULE_LOAD, (long)name, (long)data, size);
}

long sys_module_unload(const char* name) {
    return syscall1(SYS_MODULE_UNLOAD, (long)name);
}