#include <syscall.h>

long syscall0(long n) {
    long ret;
    __asm__ volatile(
        "mov %1, %%rax;"
        "int $0x80;"
        "mov %%rax, %0;"
        : "=r"(ret)
        : "r"(n)
        : "rax", "rdi", "rsi", "rdx", "memory"
    );
    return ret;
}

long syscall1(long n, long a1) {
    long ret;
    __asm__ volatile(
        "mov %1, %%rax;"
        "mov %2, %%rdi;"
        "int $0x80;"
        "mov %%rax, %0;"
        : "=r"(ret)
        : "r"(n), "r"(a1)
        : "rax", "rdi", "rsi", "rdx", "memory"
    );
    return ret;
}

long syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "mov %1, %%rax;"
        "mov %2, %%rdi;"
        "mov %3, %%rsi;"
        "int $0x80;"
        "mov %%rax, %0;"
        : "=r"(ret)
        : "r"(n), "r"(a1), "r"(a2)
        : "rax", "rdi", "rsi", "rdx", "memory"
    );
    return ret;
}

long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "mov %1, %%rax;"
        "mov %2, %%rdi;"
        "mov %3, %%rsi;"
        "mov %4, %%rdx;"
        "int $0x80;"
        "mov %%rax, %0;"
        : "=r"(ret)
        : "r"(n), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "memory"
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