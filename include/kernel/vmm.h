#ifndef _KERNEL_VMM_H
#define _KERNEL_VMM_H

#include "stdint.h"

#define PAGE_SIZE 4096

#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITE      (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_WRITETHROUGH (1 << 3)
#define PAGE_CACHE_DISABLE (1 << 4)
#define PAGE_ACCESSED   (1 << 5)
#define PAGE_DIRTY      (1 << 6)
#define PAGE_HUGE       (1 << 7)
#define PAGE_GLOBAL     (1 << 8)

typedef struct page_directory {
    uint64_t* pml4;
    uint64_t pml4_phys;
} page_directory_t;

void vmm_init(void);
page_directory_t* vmm_create_address_space(void);
void vmm_switch_directory(page_directory_t* dir);
page_directory_t* vmm_get_kernel_directory(void);

bool vmm_map_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags);
bool vmm_unmap_page(page_directory_t* dir, uint64_t virt);
uint64_t vmm_get_physical(page_directory_t* dir, uint64_t virt);

void* vmm_alloc_pages(size_t count);
void vmm_free_pages(void* virt, size_t count);

bool vmm_change_flags(page_directory_t* dir, uint64_t virt, uint32_t flags);
void* vmm_temp_map(uint64_t phys);

bool vmm_map_user_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags);

#endif