#ifndef _KERNEL_TSS_H
#define _KERNEL_TSS_H

#include "stdint.h"

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
    uint32_t reserved4;
    uint32_t reserved5;
    uint64_t reserved6;
} __attribute__((packed)) tss_t;

void tss_init(void);
void tss_set_kernel_stack(uint64_t stack);
tss_t* tss_get_address(void);

#endif