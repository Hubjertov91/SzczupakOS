#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include "stdint.h"

void pmm_init(uint64_t mem_start, uint64_t mem_end);
void pmm_reserve_range(uint64_t start, uint64_t end);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(uint32_t count);
void pmm_free_page(uint64_t addr);
void pmm_free_pages(uint64_t addr, uint32_t count);
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_used_memory(void);
uint64_t pmm_get_free_memory(void);

#endif
