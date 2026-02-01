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

void syscall_init(void);

#endif