#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <kernel/spinlock.h>
#include <drivers/serial.h>

#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDP_INDEX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))
#define ALIGN_UP(addr, align)   (((addr) + (align) - 1) & ~((align) - 1))

#define PHYS_TO_VIRT(phys) ((void*)((uint64_t)(phys) + 0xFFFF800000000000ULL))
#define VIRT_TO_PHYS(virt) ((uint64_t)(virt) - 0xFFFF800000000000ULL)

static page_directory_t* kernel_directory = NULL;
static uint64_t          next_virt_addr   = 0xFFFF800100000000ULL;
static spinlock_t        vmm_lock         = SPINLOCK_INIT;

extern uint64_t p4_table;

static void memset(void* dest, int c, size_t n) {
	char* d = (char*)dest;
	while (n--) *d++ = (char)c;
}

static inline bool is_canonical(uint64_t addr) {
	return (addr <= 0x00007FFFFFFFFFFFULL) || (addr >= 0xFFFF800000000000ULL);
}

static inline void invlpg(uint64_t addr) {
	__asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static uint64_t* get_or_create_table(uint64_t* parent, size_t index, uint32_t flags) {
	if (!parent || index >= 512) return NULL;

	if (parent[index] & PAGE_PRESENT) {
		uint64_t phys_addr = parent[index] & ~0xFFFULL;

		if ((flags & PAGE_USER) && !(parent[index] & PAGE_USER)) {
			__sync_synchronize();
			parent[index] |= PAGE_USER;
			__sync_synchronize();
		}
		if ((flags & PAGE_WRITE) && !(parent[index] & PAGE_WRITE)) {
			__sync_synchronize();
			parent[index] |= PAGE_WRITE;
			__sync_synchronize();
		}

		return PHYS_TO_VIRT(phys_addr);
	}

	uint64_t table_phys   = pmm_alloc_page();
	if (!table_phys) return NULL;

	uint64_t* table       = PHYS_TO_VIRT(table_phys);
	uint32_t  table_flags = PAGE_PRESENT | PAGE_WRITE;
	if (flags & PAGE_USER) table_flags |= PAGE_USER;

	memset(table, 0, PAGE_SIZE);
	__sync_synchronize();
	parent[index] = table_phys | table_flags;

	return table;
}

bool vmm_init(void) {
	kernel_directory = (page_directory_t*)kmalloc(sizeof(page_directory_t));
	if (!kernel_directory) {
		serial_write("[VMM] ERROR: Failed to allocate kernel directory\n");
		return false;
	}
	kernel_directory->pml4 = (uint64_t*)&p4_table;
	uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	kernel_directory->pml4_phys = cr3;
	serial_write("[VMM] Virtual memory initialized\n");
	return true;
}

page_directory_t* vmm_get_kernel_directory(void) {
	return kernel_directory;
}

page_directory_t* vmm_create_address_space(void) {
	page_directory_t* dir = (page_directory_t*)kmalloc(sizeof(page_directory_t));
	if (!dir) {
		serial_write("[VMM] Failed to allocate dir struct\n");
		return NULL;
	}

	uint64_t pml4_phys = pmm_alloc_page();
	if (!pml4_phys) {
		serial_write("[VMM] Failed to allocate PML4 page\n");
		kfree(dir);
		return NULL;
	}

	uint64_t* pml4 = vmm_temp_map(pml4_phys);
	if (!pml4) {
		serial_write("[VMM] Failed to temp map PML4\n");
		pmm_free_page(pml4_phys);
		kfree(dir);
		return NULL;
	}

	uint64_t* kernel_pml4 = vmm_temp_map(kernel_directory->pml4_phys);
	if (!kernel_pml4) {
		serial_write("[VMM] Failed to temp map kernel PML4\n");
		pmm_free_page(pml4_phys);
		kfree(dir);
		return NULL;
	}

	dir->pml4      = pml4;
	dir->pml4_phys = pml4_phys;

	for (int i = 0; i < 512; i++) pml4[i] = 0;

	pml4[0] = kernel_pml4[0];
	for (int i = 256; i < 512; i++) pml4[i] = kernel_pml4[i];

	serial_write("[VMM] Created address space successfully\n");
	return dir;
}

void vmm_destroy_address_space(page_directory_t* dir) {
	if (!dir || dir == kernel_directory) return;

	irq_state_t state = spinlock_acquire_irqsave(&vmm_lock);
	uint64_t* pml4 = PHYS_TO_VIRT(dir->pml4_phys);

	for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
		if (!(pml4[pml4_idx] & PAGE_PRESENT)) continue;
		uint64_t* pdp = PHYS_TO_VIRT(pml4[pml4_idx] & ~0xFFFULL);

		for (int pdp_idx = 0; pdp_idx < 512; pdp_idx++) {
			if (!(pdp[pdp_idx] & PAGE_PRESENT)) continue;
			uint64_t* pd = PHYS_TO_VIRT(pdp[pdp_idx] & ~0xFFFULL);

			for (int pd_idx = 0; pd_idx < 512; pd_idx++) {
				if (!(pd[pd_idx] & PAGE_PRESENT)) continue;
				if (pd[pd_idx] & (1ULL << 7)) continue;
				uint64_t pt_phys = pd[pd_idx] & ~0xFFFULL;
				pmm_free_page(pt_phys);
			}
			pmm_free_page(pdp[pdp_idx] & ~0xFFFULL);
		}
		pmm_free_page(pml4[pml4_idx] & ~0xFFFULL);
	}

	pmm_free_page(dir->pml4_phys);
	kfree(dir);
	spinlock_release_irqrestore(&vmm_lock, state);
}

void vmm_switch_directory(page_directory_t* dir) {
	if (!dir) return;
	__asm__ volatile("mov %0, %%cr3" : : "r"(dir->pml4_phys) : "memory");
}

static bool split_huge_page(uint64_t* pd, size_t pd_idx) {
	if (!(pd[pd_idx] & (1ULL << 7))) return true;

	uint64_t huge_phys = pd[pd_idx] & ~0x1FFFFFULL;
	uint64_t flags     = pd[pd_idx] & 0xFFF;
	uint64_t pt_phys   = pmm_alloc_page();
	if (!pt_phys) return false;

	uint64_t* pt = PHYS_TO_VIRT(pt_phys);
	for (int i = 0; i < 512; i++)
		pt[i] = (huge_phys + i * 4096) | (flags & ~(1ULL << 7)) | PAGE_PRESENT;

	__sync_synchronize();
	pd[pd_idx] = pt_phys | (flags & ~(1ULL << 7)) | PAGE_PRESENT;
	return true;
}

bool vmm_map_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags) {
	if (!dir || !is_canonical(virt)) return false;

	virt &= ~0xFFFULL;
	phys &= ~0xFFFULL;

	size_t pml4_idx = PML4_INDEX(virt);
	size_t pdp_idx  = PDP_INDEX(virt);
	size_t pd_idx   = PD_INDEX(virt);
	size_t pt_idx   = PT_INDEX(virt);

	irq_state_t state = spinlock_acquire_irqsave(&vmm_lock);

	uint64_t* pml4 = PHYS_TO_VIRT(dir->pml4_phys);

	uint64_t* pdp = get_or_create_table(pml4, pml4_idx, flags);
	if (!pdp) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	uint64_t* pd = get_or_create_table(pdp, pdp_idx, flags);
	if (!pd)  { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	if (pd[pd_idx] & (1ULL << 7)) {
		if (!split_huge_page(pd, pd_idx)) {
			spinlock_release_irqrestore(&vmm_lock, state);
			return false;
		}
	}

	uint64_t* pt = get_or_create_table(pd, pd_idx, flags);
	if (!pt)  { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	if (pt[pt_idx] & PAGE_PRESENT) {
		uint64_t old_phys = pt[pt_idx] & ~0xFFFULL;
		if (old_phys != phys) pmm_free_page(old_phys);
	}

	__sync_synchronize();
	pt[pt_idx] = (phys & 0x000FFFFFFFFFF000ULL) | (flags & 0xFFF) | PAGE_PRESENT;

	spinlock_release_irqrestore(&vmm_lock, state);
	invlpg(virt);
	return true;
}

bool vmm_unmap_page(page_directory_t* dir, uint64_t virt) {
	if (!dir) return false;

	virt = ALIGN_DOWN(virt, PAGE_SIZE);

	size_t pml4_idx = PML4_INDEX(virt);
	size_t pdp_idx  = PDP_INDEX(virt);
	size_t pd_idx   = PD_INDEX(virt);
	size_t pt_idx   = PT_INDEX(virt);

	irq_state_t state = spinlock_acquire_irqsave(&vmm_lock);

	uint64_t* pml4 = PHYS_TO_VIRT(dir->pml4_phys);
	if (!(pml4[pml4_idx] & PAGE_PRESENT)) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	uint64_t* pdp = PHYS_TO_VIRT(pml4[pml4_idx] & ~0xFFFULL);
	if (!(pdp[pdp_idx] & PAGE_PRESENT)) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	uint64_t* pd = PHYS_TO_VIRT(pdp[pdp_idx] & ~0xFFFULL);
	if (!(pd[pd_idx] & PAGE_PRESENT)) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	if (pd[pd_idx] & (1ULL << 7)) {
		__sync_synchronize();
		pd[pd_idx] = 0;
		spinlock_release_irqrestore(&vmm_lock, state);
		invlpg(virt);
		return true;
	}

	uint64_t* pt = PHYS_TO_VIRT(pd[pd_idx] & ~0xFFFULL);
	__sync_synchronize();
	pt[pt_idx] = 0;

	spinlock_release_irqrestore(&vmm_lock, state);
	invlpg(virt);
	return true;
}

void* vmm_temp_map(uint64_t phys) {
	return PHYS_TO_VIRT(phys);
}

uint64_t vmm_get_physical(page_directory_t* dir, uint64_t virt) {
	if (!dir) return 0;

	uint64_t orig = virt;
	virt = ALIGN_DOWN(virt, PAGE_SIZE);

	size_t pml4_idx = PML4_INDEX(virt);
	size_t pdp_idx  = PDP_INDEX(virt);
	size_t pd_idx   = PD_INDEX(virt);
	size_t pt_idx   = PT_INDEX(virt);

	uint64_t* pml4 = PHYS_TO_VIRT(dir->pml4_phys);
	if (!(pml4[pml4_idx] & PAGE_PRESENT)) return 0;

	uint64_t* pdp = PHYS_TO_VIRT(pml4[pml4_idx] & ~0xFFFULL);
	if (!(pdp[pdp_idx] & PAGE_PRESENT)) return 0;
	if (pdp[pdp_idx] & (1ULL << 7)) return (pdp[pdp_idx] & ~0x3FFFFFFFULL) + (orig & 0x3FFFFFFF);

	uint64_t* pd = PHYS_TO_VIRT(pdp[pdp_idx] & ~0xFFFULL);
	if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
	if (pd[pd_idx] & (1ULL << 7)) return (pd[pd_idx] & ~0x1FFFFFULL) + (orig & 0x1FFFFF);

	uint64_t* pt = PHYS_TO_VIRT(pd[pd_idx] & ~0xFFFULL);
	if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
	return (pt[pt_idx] & ~0xFFFULL) + (orig & 0xFFF);
}

void* vmm_alloc_pages(size_t count) {
	if (!count) return NULL;

	irq_state_t state = spinlock_acquire_irqsave(&vmm_lock);
	uint64_t virt_start  = next_virt_addr;
	next_virt_addr      += count * PAGE_SIZE;
	spinlock_release_irqrestore(&vmm_lock, state);

	uint64_t phys_base = pmm_alloc_pages(count);
	if (!phys_base) return NULL;

	for (size_t i = 0; i < count; i++) {
		if (!vmm_map_page(kernel_directory,
		                  virt_start + i * PAGE_SIZE,
		                  phys_base  + i * PAGE_SIZE,
		                  PAGE_PRESENT | PAGE_WRITE)) {
			for (size_t j = 0; j < i; j++)
				vmm_unmap_page(kernel_directory, virt_start + j * PAGE_SIZE);
			pmm_free_pages(phys_base, count);
			return NULL;
		}
	}

	return (void*)virt_start;
}

void vmm_free_pages(void* virt, size_t count) {
	if (!virt) return;

	uint64_t addr = (uint64_t)virt;
	for (size_t i = 0; i < count; i++) {
		uint64_t phys = vmm_get_physical(kernel_directory, addr + i * PAGE_SIZE);
		if (phys) pmm_free_page(phys);
		vmm_unmap_page(kernel_directory, addr + i * PAGE_SIZE);
	}
}

bool vmm_change_flags(page_directory_t* dir, uint64_t virt, uint32_t flags) {
	if (!dir) return false;

	virt = ALIGN_DOWN(virt, PAGE_SIZE);

	size_t pml4_idx = PML4_INDEX(virt);
	size_t pdp_idx  = PDP_INDEX(virt);
	size_t pd_idx   = PD_INDEX(virt);
	size_t pt_idx   = PT_INDEX(virt);

	irq_state_t state = spinlock_acquire_irqsave(&vmm_lock);

	uint64_t* pml4 = PHYS_TO_VIRT(dir->pml4_phys);
	if (!(pml4[pml4_idx] & PAGE_PRESENT)) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	uint64_t* pdp = PHYS_TO_VIRT(pml4[pml4_idx] & ~0xFFFULL);
	if (!(pdp[pdp_idx] & PAGE_PRESENT)) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	uint64_t* pd = PHYS_TO_VIRT(pdp[pdp_idx] & ~0xFFFULL);
	if (!(pd[pd_idx] & PAGE_PRESENT)) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	if (pd[pd_idx] & (1ULL << 7)) {
		if (!split_huge_page(pd, pd_idx)) {
			spinlock_release_irqrestore(&vmm_lock, state);
			return false;
		}
	}

	uint64_t* pt = PHYS_TO_VIRT(pd[pd_idx] & ~0xFFFULL);
	if (!(pt[pt_idx] & PAGE_PRESENT)) { spinlock_release_irqrestore(&vmm_lock, state); return false; }

	uint64_t phys = pt[pt_idx] & ~0xFFFULL;
	__sync_synchronize();
	pt[pt_idx] = phys | flags | PAGE_PRESENT;

	spinlock_release_irqrestore(&vmm_lock, state);
	invlpg(virt);
	return true;
}

bool vmm_map_user_page(page_directory_t* dir, uint64_t virt, uint64_t phys, uint32_t flags) {
	if (!dir || !is_canonical(virt)) return false;

	virt  &= ~0xFFFULL;
	phys  &= ~0xFFFULL;
	flags |= PAGE_USER;

	size_t pml4_idx = PML4_INDEX(virt);
	size_t pdp_idx  = PDP_INDEX(virt);
	size_t pd_idx   = PD_INDEX(virt);
	size_t pt_idx   = PT_INDEX(virt);

	irq_state_t state = spinlock_acquire_irqsave(&vmm_lock);

	uint64_t* pml4 = PHYS_TO_VIRT(dir->pml4_phys);

	uint64_t* pdp = get_or_create_table(pml4, pml4_idx, flags);
	if (!pdp) { spinlock_release_irqrestore(&vmm_lock, state); return false; }
	pml4[pml4_idx] |= PAGE_USER;

	uint64_t* pd = get_or_create_table(pdp, pdp_idx, flags);
	if (!pd)  { spinlock_release_irqrestore(&vmm_lock, state); return false; }
	pdp[pdp_idx] |= PAGE_USER;

	if (pd[pd_idx] & (1ULL << 7)) {
		if (!split_huge_page(pd, pd_idx)) {
			spinlock_release_irqrestore(&vmm_lock, state);
			return false;
		}
	}

	uint64_t* pt = get_or_create_table(pd, pd_idx, flags);
	if (!pt)  { spinlock_release_irqrestore(&vmm_lock, state); return false; }
	pd[pd_idx] |= PAGE_USER;

	if (pt[pt_idx] & PAGE_PRESENT) {
		uint64_t old_phys = pt[pt_idx] & ~0xFFFULL;
		if (old_phys != phys) pmm_free_page(old_phys);
	}

	__sync_synchronize();
	pt[pt_idx] = phys | flags | PAGE_PRESENT;

	spinlock_release_irqrestore(&vmm_lock, state);
	invlpg(virt);
	return true;
}