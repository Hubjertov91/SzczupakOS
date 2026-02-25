#include <drivers/serial.h>
#include <kernel/stdint.h>
#include <kernel/vga.h>
#include <mm/pmm.h>
#include <kernel/spinlock.h>

#define PAGE_SIZE 4096

uint64_t pmm_alloc_pages(uint32_t count);
void pmm_free_pages(uint64_t addr, uint32_t count);

extern uint8_t kernel_end;
extern uint64_t p4_table;
extern uint64_t p3_table;
extern uint64_t p2_table;

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define MAX_MEMORY_PAGES (1024 * 1024 * 1024 / 4096)
#define BITMAP_SIZE ((MAX_MEMORY_PAGES + 63) / 64)
#define PMM_PHYS_TO_VIRT(phys) ((uint64_t*)((uint64_t)(phys) + 0xFFFF800000000000ULL))

static uint64_t pmm_bitmap[BITMAP_SIZE];
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_used_pages = 0;
static uint64_t pmm_start = 0;
static uint64_t pmm_end = 0;
static spinlock_t pmm_lock = SPINLOCK_INIT;
static uint64_t last_alloc_hint = 0;

static void* memset_pmm(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static inline void bitmap_set_bit(uint64_t bit) {
    if (bit >= pmm_total_pages) return;
    pmm_bitmap[bit / 64] |= (1ULL << (bit % 64));
}

static inline void bitmap_clear_bit(uint64_t bit) {
    if (bit >= pmm_total_pages) return;
    pmm_bitmap[bit / 64] &= ~(1ULL << (bit % 64));
}

static inline bool bitmap_test_bit(uint64_t bit) {
    if (bit >= pmm_total_pages) return true;
    return (pmm_bitmap[bit / 64] & (1ULL << (bit % 64))) != 0;
}

static inline bool bitmap_range_is_clear(uint64_t start_page, uint64_t count) {
    if (count == 0) return false;
    if (start_page >= pmm_total_pages) return false;
    if (start_page + count > pmm_total_pages) return false;

    for (uint64_t i = 0; i < count; i++) {
        if (bitmap_test_bit(start_page + i)) return false;
    }
    return true;
}

static void pmm_mark_used_locked(uint64_t start, uint64_t end) {
    if (start >= end) return;
    uint64_t page_start = ALIGN_DOWN(start, PAGE_SIZE);
    uint64_t page_end = ALIGN_UP(end, PAGE_SIZE);

    for (uint64_t addr = page_start; addr < page_end; addr += PAGE_SIZE) {
        if (addr < pmm_start || addr >= pmm_end) continue;
        uint64_t page = (addr - pmm_start) / PAGE_SIZE;
        if (!bitmap_test_bit(page)) {
            bitmap_set_bit(page);
            pmm_used_pages++;
        }
    }
}

static void pmm_mark_used(uint64_t start, uint64_t end) {
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    pmm_mark_used_locked(start, end);
    spinlock_release_irqrestore(&pmm_lock, state);
}

void pmm_reserve_range(uint64_t start, uint64_t end) {
    if (start >= end) return;

    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    pmm_mark_used_locked(start, end);
    spinlock_release_irqrestore(&pmm_lock, state);
}

void pmm_init(uint64_t mem_start, uint64_t mem_end) {
    if (mem_start >= mem_end) {
        serial_write("[PMM] ERROR: Invalid memory range\n");
        return;
    }
    
    pmm_start = mem_start;
    pmm_end = mem_end;
    pmm_total_pages = (mem_end - mem_start) / PAGE_SIZE;
    
    if (pmm_total_pages == 0) {
        serial_write("[PMM] ERROR: Zero total pages\n");
        return;
    }
    
    if (pmm_total_pages > MAX_MEMORY_PAGES) {
        pmm_total_pages = MAX_MEMORY_PAGES;
        pmm_end = pmm_start + pmm_total_pages * PAGE_SIZE;
    }
    
    uint64_t bitmap_entries = (pmm_total_pages + 63) / 64;
    for (uint64_t i = 0; i < bitmap_entries; i++) {
        pmm_bitmap[i] = 0x0ULL;
    }
    pmm_used_pages = 0;
    
    if (pmm_start < (2ULL * 1024 * 1024)) {
        pmm_mark_used(pmm_start, 2ULL * 1024 * 1024);
    }
    
    uint64_t kernel_end_addr = ALIGN_UP((uint64_t)&kernel_end, PAGE_SIZE);
    pmm_mark_used(0x100000, kernel_end_addr);
    pmm_mark_used((uint64_t)&p4_table, (uint64_t)&p4_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)&p3_table, (uint64_t)&p3_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)&p2_table, (uint64_t)&p2_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)pmm_bitmap, (uint64_t)pmm_bitmap + BITMAP_SIZE * 8);
    
    serial_write("[PMM] Bitmap allocator initialized\n");
    serial_write("[PMM] Managing ");
    serial_write_dec((pmm_total_pages * PAGE_SIZE) / (1024 * 1024));
    serial_write(" MB (Used: ");
    serial_write_dec(pmm_used_pages);
    serial_write(" Free: ");
    serial_write_dec(pmm_total_pages - pmm_used_pages);
    serial_write(" pages)\n");
    vga_write("[PMM] initialized\n");
}

uint64_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

uint64_t pmm_alloc_pages(uint32_t count) {
    if (count == 0) return 0;
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);

    uint64_t hint = last_alloc_hint % pmm_total_pages;
    for (uint64_t i = 0; i < pmm_total_pages; i++) {
        uint64_t page = (hint + i) % pmm_total_pages;
        if (page + count > pmm_total_pages) continue;
        if (!bitmap_range_is_clear(page, count)) continue;

        for (uint32_t j = 0; j < count; j++) {
            bitmap_set_bit(page + j);
        }
        pmm_used_pages += count;
        last_alloc_hint = page + count;

        uint64_t result = pmm_start + page * PAGE_SIZE;
        memset_pmm((void*)PMM_PHYS_TO_VIRT(result), 0, (uint64_t)count * PAGE_SIZE);
        spinlock_release_irqrestore(&pmm_lock, state);
        return result;
    }

    serial_write("[PMM] OUT OF MEMORY\n");
    spinlock_release_irqrestore(&pmm_lock, state);
    return 0;
}

void pmm_free_page(uint64_t addr) {
    pmm_free_pages(addr, 1);
}

void pmm_free_pages(uint64_t addr, uint32_t count) {
    if (count == 0) return;
    if (addr < pmm_start || addr >= pmm_end) return;
    if (addr & (PAGE_SIZE - 1)) {
        serial_write("[PMM] WARNING: Unaligned free 0x");
        serial_write_hex(addr);
        serial_write("\n");
        return;
    }
    
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    
    uint64_t page = (addr - pmm_start) / PAGE_SIZE;

    if (page + count > pmm_total_pages) {
        serial_write("[PMM] WARNING: Free range out of bounds 0x");
        serial_write_hex(addr);
        serial_write("\n");
        spinlock_release_irqrestore(&pmm_lock, state);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!bitmap_test_bit(page + i)) {
            serial_write("[PMM] WARNING: Double free 0x");
            serial_write_hex(addr + (uint64_t)i * PAGE_SIZE);
            serial_write("\n");
            spinlock_release_irqrestore(&pmm_lock, state);
            return;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        bitmap_clear_bit(page + i);
    }
    pmm_used_pages -= count;
    if (page < last_alloc_hint) last_alloc_hint = page;
    
    spinlock_release_irqrestore(&pmm_lock, state);
}

uint64_t pmm_get_total_memory(void) {
    return pmm_total_pages * PAGE_SIZE;
}

uint64_t pmm_get_used_memory(void) {
    return pmm_used_pages * PAGE_SIZE;
}

uint64_t pmm_get_free_memory(void) {
    if (pmm_used_pages >= pmm_total_pages) return 0;
    return (pmm_total_pages - pmm_used_pages) * PAGE_SIZE;
}
