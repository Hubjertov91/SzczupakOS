#ifndef _KERNEL_PAGEFAULT_H
#define _KERNEL_PAGEFAULT_H

#include "stdint.h"

#define PF_PRESENT  (1 << 0)
#define PF_WRITE    (1 << 1)
#define PF_USER     (1 << 2)
#define PF_RESERVED (1 << 3)
#define PF_FETCH    (1 << 4)

bool pagefault_init(void);
void pagefault_handler(uint64_t error_code, uint64_t faulting_addr);

#endif