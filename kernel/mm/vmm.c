#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/serial.h>
#include <kernel/vga.h>

static bool split_huge_page(uint64_t* pd, size_t pd_idx);

#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDP_INDEX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))
#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

static page_directory_t* kernel_directory = NULL;
static uint64_t next_virt_addr = 0x100000000;

extern uint64_t p4_table;

static uint64_t* get_or_create_table(uint64_t* parent, size_t index, uint32_t flags) {
    if (!parent) return NULL;
    if (index >= 512) {
        serial_write("[VMM] ERROR: Invalid table index\n");
        return NULL;
    }
    
    if (parent[index] & PAGE_PRESENT) {
        uint64_t phys_addr = parent[index] & ~0xFFFULL;
        
        if (flags & PAGE_USER) {
            parent[index] |= PAGE_USER;
        }
        
        return (uint64_t*)phys_addr;
    }
    
    uint64_t table_phys = pmm_alloc_page();
    if (!table_phys) {
        serial_write("[VMM] ERROR: Failed to allocate page table\n");
        return NULL;
    }
    
    uint64_t* table = (uint64_t*)table_phys;
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }
    
    uint32_t table_flags = PAGE_PRESENT | PAGE_WRITE;
    if (flags & PAGE_USER) {
        table_flags |= PAGE_USER;
    }
    
    parent[index] = table_phys | table_flags;
    
    return table;
}

void vmm_init(void) {
    kernel_directory = (page_directory_t*)kmalloc(sizeof(page_directory_t));
    if (!kernel_directory) {
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_write("[VMM] PANIC: Failed to allocate kernel directory\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        while (1) __asm__ volatile("hlt");
    }
    
    kernel_directory->pml4 = (uint64_t*)&p4_table;
    
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_directory->pml4_phys = cr3;
    
    serial_write("[VMM] Virtual Memory Manager initialized\n");
    serial_write("[VMM] Kernel PML4 at: 0x");
    serial_write_hex(cr3);
    serial_write("\n");
}

page_directory_t* vmm_get_kernel_directory(void) {
    return kernel_directory;
}

page_directory_t* vmm_create_address_space(void) {
    page_directory_t* dir = (page_directory_t*)kmalloc(sizeof(page_directory_t));
    if (!dir) return NULL;
    
    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys) {
        kfree(dir);
        return NULL;
    }
    
    uint64_t* pml4 = (uint64_t*)pml4_phys;
    dir->pml4 = pml4;
    dir->pml4_phys = pml4_phys;
    
    for (int i = 0; i < 512; i++) {
        pml4[i] = 0;
    }
    
    uint64_t* kernel_pml4 = kernel_directory->pml4;
    for (int i = 0; i < 512; i++) {
        pml4[i] = kernel_pml4[i];
    }
    
    serial_write("[VMM] Created address space, PML4 at 0x");
    serial_write_hex(pml4_phys);
    serial_write("\n");
    serial_write("[VMM] Copied kernel PML4[0]=0x");
    serial_write_hex(pml4[0]);
    serial_write("\n");
    
    return dir;
}

void vmm_switch_directory(page_directory_t* dir) {
    if (!dir) return;
    __asm__ volatile("mov %0, %%cr3" : : "r"(dir->pml4_phys) : "memory");
}

bool vmm_map_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags) {
    if (!dir) {
        serial_write("[VMM] dir is NULL!\n");
        return false;
    }
    
    serial_write("[VMM] Map: virt=0x");
    serial_write_hex(virt);
    serial_write(" -> phys=0x");
    serial_write_hex(phys);
    serial_write(" flags=0x");
    serial_write_hex(flags);
    serial_write("\n");
    
    serial_write("[VMM] dir->pml4=0x");
    serial_write_hex((uint64_t)dir->pml4);
    serial_write(" dir->pml4_phys=0x");
    serial_write_hex(dir->pml4_phys);
    serial_write("\n");
    
    virt = virt & ~0xFFFULL;
    phys = phys & ~0xFFFULL;
    
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdp_idx  = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;
    
    serial_write("[VMM] Indices: PML4=");
    serial_write_dec(pml4_idx);
    serial_write(" PDP=");
    serial_write_dec(pdp_idx);
    serial_write(" PD=");
    serial_write_dec(pd_idx);
    serial_write(" PT=");
    serial_write_dec(pt_idx);
    serial_write("\n");
    
    serial_write("[VMM] PML4[0] before=0x");
    serial_write_hex(dir->pml4[pml4_idx]);
    serial_write("\n");
    
    uint64_t* pdp = get_or_create_table(dir->pml4, pml4_idx, flags);
    if (!pdp) {
        serial_write("[VMM] Failed at PDP\n");
        return false;
    }
    serial_write("[VMM] PML4[0] after PDP=0x");
    serial_write_hex(dir->pml4[pml4_idx]);
    serial_write(" pdp=0x");
    serial_write_hex((uint64_t)pdp);
    serial_write("\n");
    
    uint64_t* pd = get_or_create_table(pdp, pdp_idx, flags);
    if (!pd) {
        serial_write("[VMM] Failed at PD\n");
        return false;
    }
    serial_write("[VMM] PDP[0] after PD=0x");
    serial_write_hex(pdp[pdp_idx]);
    serial_write(" pd=0x");
    serial_write_hex((uint64_t)pd);
    serial_write("\n");
    
    if (pd[pd_idx] & (1ULL << 7)) {
        serial_write("[VMM] Splitting huge page at PD[");
        serial_write_dec(pd_idx);
        serial_write("]\n");
        if (!split_huge_page(pd, pd_idx)) {
            serial_write("[VMM] Split failed\n");
            return false;
        }
    }
    
    uint64_t* pt = get_or_create_table(pd, pd_idx, flags);
    if (!pt) {
        serial_write("[VMM] Failed at PT\n");
        return false;
    }
    serial_write("[VMM] PD[");
    serial_write_dec(pd_idx);
    serial_write("] after PT=0x");
    serial_write_hex(pd[pd_idx]);
    serial_write(" pt=0x");
    serial_write_hex((uint64_t)pt);
    serial_write("\n");
    
    pt[pt_idx] = phys | flags | PAGE_PRESENT;
    
    serial_write("[VMM] PT[");
    serial_write_dec(pt_idx);
    serial_write("]=0x");
    serial_write_hex(pt[pt_idx]);
    serial_write("\n");
    
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    
    serial_write("[VMM] Map OK\n");
    return true;
}

bool vmm_unmap_page(page_directory_t* dir, uint64_t virt) {
    if (!dir) return false;
    
    virt = ALIGN_DOWN(virt, PAGE_SIZE);
    
    size_t pml4_idx = PML4_INDEX(virt);
    size_t pdp_idx = PDP_INDEX(virt);
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);
    
    if (!(dir->pml4[pml4_idx] & PAGE_PRESENT)) return false;
    uint64_t* pdp = (uint64_t*)(dir->pml4[pml4_idx] & ~0xFFFULL);
    
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) return false;
    uint64_t* pd = (uint64_t*)(pdp[pdp_idx] & ~0xFFFULL);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return false;
    
    if (pd[pd_idx] & (1ULL << 7)) {
        pd[pd_idx] = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
        return true;
    }
    
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);
    
    pt[pt_idx] = 0;
    
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    
    return true;
}

void* vmm_temp_map(uint64_t phys) {
    static uint64_t temp_addr = 0xFFFFFE0000000000;
    
    vmm_unmap_page(vmm_get_kernel_directory(), temp_addr);
    
    if (!vmm_map_page(vmm_get_kernel_directory(), temp_addr, phys, 
                     PAGE_PRESENT | PAGE_WRITE)) {
        return NULL;
    }
    
    return (void*)temp_addr;
}

uint64_t vmm_get_physical(page_directory_t* dir, uint64_t virt) {
    if (!dir) return 0;
    
    uint64_t original_virt = virt;
    virt = ALIGN_DOWN(virt, PAGE_SIZE);
    
    size_t pml4_idx = PML4_INDEX(virt);
    size_t pdp_idx = PDP_INDEX(virt);
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);
    
    uint64_t* pml4 = (uint64_t*)dir->pml4;
    
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pdp = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);
    
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pd = (uint64_t*)(pdp[pdp_idx] & ~0xFFFULL);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    
    if (pd[pd_idx] & (1ULL << 7)) {
        uint64_t phys_base = pd[pd_idx] & ~0x1FFFFFULL;
        uint64_t offset = original_virt & 0x1FFFFF;
        return phys_base + offset;
    }
    
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    
    uint64_t phys_base = pt[pt_idx] & ~0xFFFULL;
    uint64_t offset = original_virt & 0xFFF;
    return phys_base + offset;
}

void* vmm_alloc_pages(size_t count) {
    if (count == 0) return NULL;
    
    uint64_t virt_start = next_virt_addr;
    next_virt_addr += count * PAGE_SIZE;
    
    for (size_t i = 0; i < count; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(kernel_directory, virt_start + j * PAGE_SIZE);
            }
            return NULL;
        }
        
        if (!vmm_map_page(kernel_directory, virt_start + i * PAGE_SIZE, phys, 
                         PAGE_PRESENT | PAGE_WRITE)) {
            pmm_free_page(phys);
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(kernel_directory, virt_start + j * PAGE_SIZE);
            }
            return NULL;
        }
    }
    
    return (void*)virt_start;
}

void vmm_free_pages(void* virt, size_t count) {
    if (!virt) return;
    
    uint64_t virt_addr = (uint64_t)virt;
    
    for (size_t i = 0; i < count; i++) {
        uint64_t phys = vmm_get_physical(kernel_directory, virt_addr + i * PAGE_SIZE);
        if (phys) {
            pmm_free_page(phys);
        }
        vmm_unmap_page(kernel_directory, virt_addr + i * PAGE_SIZE);
    }
}

static bool split_huge_page(uint64_t* pd, size_t pd_idx) {
    if (!(pd[pd_idx] & (1ULL << 7))) {
        return true;
    }
    
    uint64_t huge_phys = pd[pd_idx] & ~0x1FFFFFULL;
    uint64_t flags = pd[pd_idx] & 0xFFF;
    
    uint64_t pt_phys = pmm_alloc_page();
    if (!pt_phys) return false;
    
    uint64_t* pt = (uint64_t*)pt_phys;
    for (int i = 0; i < 512; i++) {
        pt[i] = (huge_phys + i * 4096) | (flags & ~(1ULL << 7)) | PAGE_PRESENT;
    }
    
    pd[pd_idx] = pt_phys | (flags & ~(1ULL << 7)) | PAGE_PRESENT;
    
    return true;
}

bool vmm_change_flags(page_directory_t* dir, uint64_t virt, uint32_t flags) {
    if (!dir) return false;
    
    virt = ALIGN_DOWN(virt, PAGE_SIZE);
    
    size_t pml4_idx = PML4_INDEX(virt);
    size_t pdp_idx = PDP_INDEX(virt);
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);
    
    if (!(dir->pml4[pml4_idx] & PAGE_PRESENT)) return false;
    uint64_t* pdp = (uint64_t*)(dir->pml4[pml4_idx] & ~0xFFFULL);
    
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) return false;
    uint64_t* pd = (uint64_t*)(pdp[pdp_idx] & ~0xFFFULL);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return false;
    
    if (pd[pd_idx] & (1ULL << 7)) {
        if (!split_huge_page(pd, pd_idx)) return false;
    }
    
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) return false;
    
    uint64_t phys = pt[pt_idx] & ~0xFFFULL;
    pt[pt_idx] = phys | flags | PAGE_PRESENT;
    
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    
    return true;
}

bool vmm_map_user_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags) {
    serial_write("[VMM] Map USER page: virt=0x");
    serial_write_hex(virt);
    serial_write(" -> phys=0x");
    serial_write_hex(phys);
    serial_write(" user_flags=0x");
    serial_write_hex(flags);
    serial_write(" adding PAGE_USER\n");
    
    return vmm_map_page(dir, virt, phys, flags | PAGE_USER);
}