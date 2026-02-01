#ifndef _KERNEL_GDT_H
#define _KERNEL_GDT_H

#include "stdint.h"

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t stack);

#endif