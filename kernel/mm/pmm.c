// kernel/mm/pmm.c
#include <kernel/serial.h>
#include <kernel/stdint.h>
#include <kernel/vga.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>

extern uint8_t kernel_end;
extern uint64_t p4_table;
extern uint64_t p3_table;
extern uint64_t p2_table;

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define MAX_MEMORY_PAGES (256 * 1024 * 1024 / 4096)
#define BITMAP_SIZE ((MAX_MEMORY_PAGES + 31) / 32)

static uint32_t pmm_bitmap[BITMAP_SIZE];
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_used_pages = 0;
static uint64_t pmm_start = 0;
static uint64_t pmm_end = 0;
static spinlock_t pmm_lock = SPINLOCK_INIT;

static inline void bitmap_set(uint64_t bit) {
    if (bit >= pmm_total_pages) return;
    pmm_bitmap[bit / 32] |= (1u << (bit % 32));
}

static inline void bitmap_clear(uint64_t bit) {
    if (bit >= pmm_total_pages) return;
    pmm_bitmap[bit / 32] &= ~(1u << (bit % 32));
}

static inline bool bitmap_test(uint64_t bit) {
    if (bit >= pmm_total_pages) return true;
    return (pmm_bitmap[bit / 32] & (1u << (bit % 32))) != 0;
}

static void pmm_mark_used(uint64_t start, uint64_t end) {
    if (start >= end) return;
    uint64_t page_start = ALIGN_DOWN(start, PAGE_SIZE);
    uint64_t page_end = ALIGN_UP(end, PAGE_SIZE);
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    for (uint64_t addr = page_start; addr < page_end; addr += PAGE_SIZE) {
        if (addr < pmm_start || addr >= pmm_end) continue;
        uint64_t page = (addr - pmm_start) / PAGE_SIZE;
        if (!bitmap_test(page)) {
            bitmap_set(page);
            pmm_used_pages++;
        }
    }
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
    uint32_t bitmap_entries = (pmm_total_pages + 31) / 32;
    for (uint32_t i = 0; i < bitmap_entries; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
    pmm_used_pages = pmm_total_pages;
    uint64_t free_start = 2 * 1024 * 1024;
    for (uint64_t addr = free_start; addr < pmm_end; addr += PAGE_SIZE) {
        uint64_t page = (addr - pmm_start) / PAGE_SIZE;
        if (page < pmm_total_pages) {
            bitmap_clear(page);
            pmm_used_pages--;
        }
    }
    uint64_t kernel_end_addr = ALIGN_UP((uint64_t)&kernel_end, PAGE_SIZE);
    pmm_mark_used(0x100000, kernel_end_addr);
    pmm_mark_used((uint64_t)&p4_table, (uint64_t)&p4_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)&p3_table, (uint64_t)&p3_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)&p2_table, (uint64_t)&p2_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)pmm_bitmap, (uint64_t)pmm_bitmap + sizeof(pmm_bitmap));
    serial_write("[PMM] Physical Memory Manager initialized\n");
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
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    for (uint64_t i = 0; i < pmm_total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            pmm_used_pages++;
            uint64_t result = pmm_start + i * PAGE_SIZE;
            spinlock_release_irqrestore(&pmm_lock, state);
            return result;
        }
    }
    serial_write("[PMM] OUT OF MEMORY\n");
    spinlock_release_irqrestore(&pmm_lock, state);
    return 0;
}

void pmm_free_page(uint64_t addr) {
    if (addr < pmm_start || addr >= pmm_end) return;
    if (addr & (PAGE_SIZE - 1)) {
        serial_write("[PMM] WARNING: Unaligned free 0x");
        serial_write_hex(addr);
        serial_write("\n");
        return;
    }
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    uint64_t page = (addr - pmm_start) / PAGE_SIZE;
    if (!bitmap_test(page)) {
        serial_write("[PMM] WARNING: Double free 0x");
        serial_write_hex(addr);
        serial_write("\n");
        spinlock_release_irqrestore(&pmm_lock, state);
        return;
    }
    bitmap_clear(page);
    pmm_used_pages--;
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