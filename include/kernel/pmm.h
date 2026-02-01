#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include "stdint.h"

#define PAGE_SIZE 4096

void pmm_init(uint64_t mem_start, uint64_t mem_end);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t addr);
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_used_memory(void);
uint64_t pmm_get_free_memory(void);

#endif