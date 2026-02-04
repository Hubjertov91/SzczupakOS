#ifndef SYSCALL_H
#define SYSCALL_H

#include "stdint.h"

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

struct sysinfo {
    uint64_t uptime;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t nr_processes;
};

long syscall0(long n);
long syscall1(long n, long a1);
long syscall2(long n, long a1, long a2);
long syscall3(long n, long a1, long a2, long a3);

void sys_exit(int code);
long sys_write(const char* buf, long len);
long sys_read(char* buf, long len);
long sys_getpid(void);
long sys_gettime(void);
void sys_sleep(long ms);
void sys_clear(void);
long sys_sysinfo(struct sysinfo* info);

#endif