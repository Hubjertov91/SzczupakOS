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
#define BUDDY_MAX_ORDER 11

typedef struct free_area {
    uint64_t* free_list;
    uint32_t nr_free;
} free_area_t;

static uint64_t pmm_bitmap[BITMAP_SIZE];
static free_area_t free_area[BUDDY_MAX_ORDER];
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_used_pages = 0;
static uint64_t pmm_start = 0;
static uint64_t pmm_end = 0;
static spinlock_t pmm_lock = SPINLOCK_INIT;
static uint32_t* page_orders;
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

static inline uint32_t get_order(uint64_t size) {
    uint32_t order = 0;
    size = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size--;
    while (size >>= 1) order++;
    return order;
}

static inline uint64_t get_buddy(uint64_t page, uint32_t order) {
    return page ^ (1ULL << order);
}

static void buddy_add_block(uint64_t page, uint32_t order) {
    if (order >= BUDDY_MAX_ORDER) return;
    
    uint64_t* block = (uint64_t*)(pmm_start + page * PAGE_SIZE);
    *block = (uint64_t)free_area[order].free_list;
    free_area[order].free_list = block;
    free_area[order].nr_free++;
}

static uint64_t buddy_remove_block(uint32_t order) {
    if (order >= BUDDY_MAX_ORDER || !free_area[order].free_list) return 0;
    
    uint64_t* block = free_area[order].free_list;
    free_area[order].free_list = (uint64_t*)(*block);
    free_area[order].nr_free--;
    
    uint64_t addr = (uint64_t)block;
    return (addr - pmm_start) / PAGE_SIZE;
}

static void buddy_init(void) {
    for (uint32_t i = 0; i < BUDDY_MAX_ORDER; i++) {
        free_area[i].free_list = NULL;
        free_area[i].nr_free = 0;
    }
    
    page_orders = (uint32_t*)(pmm_start + BITMAP_SIZE * 8);
    memset_pmm(page_orders, 0, pmm_total_pages * sizeof(uint32_t));
    
    uint64_t free_start = 2 * 1024 * 1024;
    uint64_t free_pages = (pmm_end - free_start) / PAGE_SIZE;
    
    for (uint64_t i = 0; i < free_pages; ) {
        uint32_t order = BUDDY_MAX_ORDER - 1;
        while (order > 0 && (i + (1ULL << order) > free_pages)) {
            order--;
        }
        
        uint64_t page = (free_start - pmm_start) / PAGE_SIZE + i;
        buddy_add_block(page, order);
        page_orders[page] = order;
        
        i += (1ULL << order);
    }
}

static void pmm_mark_used(uint64_t start, uint64_t end) {
    if (start >= end) return;
    uint64_t page_start = ALIGN_DOWN(start, PAGE_SIZE);
    uint64_t page_end = ALIGN_UP(end, PAGE_SIZE);
    
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    
    for (uint64_t addr = page_start; addr < page_end; addr += PAGE_SIZE) {
        if (addr < pmm_start || addr >= pmm_end) continue;
        uint64_t page = (addr - pmm_start) / PAGE_SIZE;
        if (!bitmap_test_bit(page)) {
            bitmap_set_bit(page);
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
    
    uint64_t bitmap_entries = (pmm_total_pages + 63) / 64;
    for (uint64_t i = 0; i < bitmap_entries; i++) {
        pmm_bitmap[i] = 0x0ULL;
    }
    pmm_used_pages = 0;
    
    buddy_init();
    
    uint64_t kernel_end_addr = ALIGN_UP((uint64_t)&kernel_end, PAGE_SIZE);
    pmm_mark_used(0x100000, kernel_end_addr);
    pmm_mark_used((uint64_t)&p4_table, (uint64_t)&p4_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)&p3_table, (uint64_t)&p3_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)&p2_table, (uint64_t)&p2_table + PAGE_SIZE);
    pmm_mark_used((uint64_t)pmm_bitmap, (uint64_t)pmm_bitmap + BITMAP_SIZE * 8);
    pmm_mark_used((uint64_t)page_orders, (uint64_t)page_orders + pmm_total_pages * sizeof(uint32_t));
    
    serial_write("[PMM] Buddy allocator initialized\n");
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
    
    uint32_t order = get_order(count * PAGE_SIZE);
    if (order >= BUDDY_MAX_ORDER) return 0;
    
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    
    uint32_t current_order = order;
    while (current_order < BUDDY_MAX_ORDER && !free_area[current_order].free_list) {
        current_order++;
    }
    
    if (current_order >= BUDDY_MAX_ORDER) {
        uint64_t hint = last_alloc_hint;
        for (uint64_t i = 0; i < pmm_total_pages; i++) {
            uint64_t idx = (hint + i) % pmm_total_pages;
            if (!bitmap_test_bit(idx)) {
                bool found = true;
                for (uint32_t j = 0; j < count; j++) {
                    if (idx + j >= pmm_total_pages || bitmap_test_bit(idx + j)) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    for (uint32_t j = 0; j < count; j++) {
                        bitmap_set_bit(idx + j);
                    }
                    pmm_used_pages += count;
                    last_alloc_hint = idx + count;
                    uint64_t result = pmm_start + idx * PAGE_SIZE;
                    spinlock_release_irqrestore(&pmm_lock, state);
                    memset_pmm((void*)result, 0, count * PAGE_SIZE);
                    return result;
                }
            }
        }
        
        serial_write("[PMM] OUT OF MEMORY\n");
        spinlock_release_irqrestore(&pmm_lock, state);
        return 0;
    }
    
    while (current_order > order) {
        uint64_t page = buddy_remove_block(current_order);
        current_order--;
        
        buddy_add_block(page, current_order);
        buddy_add_block(page + (1ULL << current_order), current_order);
        
        page_orders[page] = current_order;
        page_orders[page + (1ULL << current_order)] = current_order;
    }
    
    uint64_t page = buddy_remove_block(order);
    page_orders[page] = order;
    
    uint64_t num_pages = 1ULL << order;
    for (uint64_t i = 0; i < num_pages; i++) {
        bitmap_set_bit(page + i);
    }
    pmm_used_pages += num_pages;
    
    last_alloc_hint = page + num_pages;
    uint64_t result = pmm_start + page * PAGE_SIZE;
    
    spinlock_release_irqrestore(&pmm_lock, state);
    
    memset_pmm((void*)result, 0, num_pages * PAGE_SIZE);
    
    return result;
}

void pmm_free_page(uint64_t addr) {
    pmm_free_pages(addr, 1);
}

void pmm_free_pages(uint64_t addr, uint32_t count) {
    if (addr < pmm_start || addr >= pmm_end) return;
    if (addr & (PAGE_SIZE - 1)) {
        serial_write("[PMM] WARNING: Unaligned free 0x");
        serial_write_hex(addr);
        serial_write("\n");
        return;
    }
    
    irq_state_t state = spinlock_acquire_irqsave(&pmm_lock);
    
    uint64_t page = (addr - pmm_start) / PAGE_SIZE;
    
    if (page_orders[page] == 0) {
        for (uint32_t i = 0; i < count; i++) {
            if (!bitmap_test_bit(page + i)) {
                serial_write("[PMM] WARNING: Double free 0x");
                serial_write_hex(addr + i * PAGE_SIZE);
                serial_write("\n");
                spinlock_release_irqrestore(&pmm_lock, state);
                return;
            }
            bitmap_clear_bit(page + i);
        }
        pmm_used_pages -= count;
        spinlock_release_irqrestore(&pmm_lock, state);
        return;
    }
    
    uint32_t order = page_orders[page];
    uint64_t num_pages = 1ULL << order;
    
    for (uint64_t i = 0; i < num_pages; i++) {
        if (!bitmap_test_bit(page + i)) {
            serial_write("[PMM] WARNING: Double free in buddy 0x");
            serial_write_hex(addr);
            serial_write("\n");
            spinlock_release_irqrestore(&pmm_lock, state);
            return;
        }
        bitmap_clear_bit(page + i);
    }
    pmm_used_pages -= num_pages;
    
    while (order < BUDDY_MAX_ORDER - 1) {
        uint64_t buddy = get_buddy(page, order);
        
        if (buddy >= pmm_total_pages) break;
        if (bitmap_test_bit(buddy)) break;
        if (page_orders[buddy] != order) break;
        
        uint64_t* prev = NULL;
        uint64_t* curr = free_area[order].free_list;
        while (curr) {
            uint64_t curr_page = ((uint64_t)curr - pmm_start) / PAGE_SIZE;
            if (curr_page == buddy) {
                if (prev) {
                    *prev = *curr;
                } else {
                    free_area[order].free_list = (uint64_t*)(*curr);
                }
                free_area[order].nr_free--;
                break;
            }
            prev = curr;
            curr = (uint64_t*)(*curr);
        }
        
        if (page > buddy) page = buddy;
        order++;
        page_orders[page] = order;
    }
    
    buddy_add_block(page, order);
    page_orders[page] = order;
    
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