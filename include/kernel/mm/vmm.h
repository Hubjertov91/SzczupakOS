#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define PHYS_TO_VIRT(phys) ((void*)((uint64_t)(phys) + 0xFFFF800000000000ULL))
#define VIRT_TO_PHYS(virt) ((uint64_t)(virt) - 0xFFFF800000000000ULL)

#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITE      (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_SIZE       4096

typedef struct page_directory {
    uint64_t* pml4;
    uint64_t pml4_phys;
} page_directory_t;

bool vmm_init(void);
page_directory_t* vmm_get_kernel_directory(void);
page_directory_t* vmm_create_address_space(void);
void vmm_destroy_address_space(page_directory_t* dir);
void vmm_switch_directory(page_directory_t* dir);
bool vmm_map_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags);
bool vmm_unmap_page(page_directory_t* dir, uint64_t virt);
void* vmm_temp_map(uint64_t phys);
uint64_t vmm_get_physical(page_directory_t* dir, uint64_t virt);
void* vmm_alloc_pages(size_t count);
void vmm_free_pages(void* virt, size_t count);
bool vmm_change_flags(page_directory_t* dir, uint64_t virt, uint32_t flags);
bool vmm_map_user_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags);
void vmm_sync_kernel_mappings(page_directory_t* dir);

#endif
