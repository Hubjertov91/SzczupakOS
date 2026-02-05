#include <kernel/serial.h>
#include <kernel/vga.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/spinlock.h>
#include <kernel/stdint.h>

#define HEAP_MAGIC_HEADER 0xDEADBEEF
#define HEAP_MAGIC_FOOTER 0xCAFEBABE
#define HEAP_INITIAL_SIZE (1024 * 1024)
#define HEAP_CANARY 0xAA55AA55
#define MAX_ALLOC_SIZE (128 * 1024 * 1024)

typedef struct heap_block {
    uint32_t magic_header;
    uint32_t canary;
    size_t size;
    const char* file;
    uint32_t line;
    uint64_t timestamp;
    bool is_free;
    uint32_t checksum;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

typedef struct heap_footer {
    uint32_t magic_footer;
    uint32_t checksum;
} heap_footer_t;

static heap_block_t* heap_start = NULL;
static spinlock_t heap_lock = SPINLOCK_INIT;
static uint64_t heap_alloc_count = 0;
static uint64_t heap_free_count = 0;
static uint64_t total_allocated = 0;

static void uitoa_local(uint64_t val, char* buf) {
    int idx = 0;
    if (val == 0) {
        buf[idx++] = '0';
    } else {
        char tmp[32];
        int t = 0;
        while (val) {
            tmp[t++] = '0' + (val % 10);
            val /= 10;
        }
        for (int i = t - 1; i >= 0; i--) {
            buf[idx++] = tmp[i];
        }
    }
    buf[idx] = '\0';
}

static uint32_t calculate_checksum(heap_block_t* block) {
    uint32_t sum = 0;
    sum ^= block->magic_header;
    sum ^= block->canary;
    sum ^= (uint32_t)block->size;
    sum ^= (uint32_t)((uint64_t)block->file >> 32);
    sum ^= (uint32_t)((uint64_t)block->file & 0xFFFFFFFF);
    sum ^= block->line;
    return sum;
}

static bool validate_block(heap_block_t* block) {
    if (block->magic_header != HEAP_MAGIC_HEADER) {
        serial_write("[HEAP] CORRUPTION: Invalid header magic\n");
        return false;
    }
    if (block->canary != HEAP_CANARY) {
        serial_write("[HEAP] CORRUPTION: Canary dead\n");
        return false;
    }
    uint32_t expected = calculate_checksum(block);
    if (block->checksum != expected) {
        serial_write("[HEAP] CORRUPTION: Checksum mismatch\n");
        return false;
    }
    heap_footer_t* footer = (heap_footer_t*)((char*)block + sizeof(heap_block_t) + block->size);
    if (footer->magic_footer != HEAP_MAGIC_FOOTER) {
        serial_write("[HEAP] CORRUPTION: Invalid footer magic\n");
        return false;
    }
    return true;
}

static void* sbrk(size_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE) return NULL;
    size_t pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void* addr = NULL;
    for (size_t i = 0; i < pages_needed; i++) {
        uint64_t page = pmm_alloc_page();
        if (page == 0) {
            for (size_t j = 0; j < i; j++) {
                pmm_free_page((uint64_t)addr + j * PAGE_SIZE);
            }
            return NULL;
        }
        if (i == 0) addr = (void*)page;
    }
    return addr;
}

void heap_init(void) {
    heap_start = (heap_block_t*)sbrk(HEAP_INITIAL_SIZE);
    if (!heap_start) {
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_write("[HEAP] PANIC: Failed to initialize heap!\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        while (1) __asm__ volatile("hlt");
    }
    heap_start->magic_header = HEAP_MAGIC_HEADER;
    heap_start->canary = HEAP_CANARY;
    heap_start->size = HEAP_INITIAL_SIZE - sizeof(heap_block_t) - sizeof(heap_footer_t);
    heap_start->file = "heap_init";
    heap_start->line = 0;
    heap_start->timestamp = 0;
    heap_start->is_free = true;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    heap_start->checksum = calculate_checksum(heap_start);
    heap_footer_t* footer = (heap_footer_t*)((char*)heap_start + sizeof(heap_block_t) + heap_start->size);
    footer->magic_footer = HEAP_MAGIC_FOOTER;
    footer->checksum = heap_start->checksum;
    serial_write("[HEAP] Initialized with guards (size: 0x");
    serial_write_hex(HEAP_INITIAL_SIZE);
    serial_write(")\n");
}

static heap_block_t* find_free_block(size_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE) return NULL;
    heap_block_t* current = heap_start;
    heap_block_t* best_fit = NULL;
    size_t best_fit_size = (size_t)-1;
    while (current) {
        if (!validate_block(current)) {
            serial_write("[HEAP] PANIC: Heap corruption detected in find_free_block\n");
            while (1) __asm__ volatile("hlt");
        }
        if (current->is_free && current->size >= size) {
            if (current->size < best_fit_size) {
                best_fit = current;
                best_fit_size = current->size;
            }
        }
        current = current->next;
    }
    return best_fit;
}

static void split_block(heap_block_t* block, size_t size) {
    size_t min_split = sizeof(heap_block_t) + sizeof(heap_footer_t) + 64;
    if (block->size <= size + min_split) return;
    heap_block_t* new_block = (heap_block_t*)((char*)block + sizeof(heap_block_t) + size + sizeof(heap_footer_t));
    new_block->magic_header = HEAP_MAGIC_HEADER;
    new_block->canary = HEAP_CANARY;
    new_block->size = block->size - size - sizeof(heap_block_t) - sizeof(heap_footer_t);
    new_block->file = "split";
    new_block->line = 0;
    new_block->timestamp = 0;
    new_block->is_free = true;
    new_block->next = block->next;
    new_block->prev = block;
    new_block->checksum = calculate_checksum(new_block);
    heap_footer_t* new_footer = (heap_footer_t*)((char*)new_block + sizeof(heap_block_t) + new_block->size);
    new_footer->magic_footer = HEAP_MAGIC_FOOTER;
    new_footer->checksum = new_block->checksum;
    if (block->next) block->next->prev = new_block;
    block->next = new_block;
    block->size = size;
    heap_footer_t* footer = (heap_footer_t*)((char*)block + sizeof(heap_block_t) + block->size);
    footer->magic_footer = HEAP_MAGIC_FOOTER;
    footer->checksum = block->checksum;
}

static void coalesce_free_blocks(void) {
    if (!heap_start) return;
    heap_block_t* current = heap_start;
    while (current && current->next) {
        if (!validate_block(current)) {
            serial_write("[HEAP] PANIC: Corruption in coalesce\n");
            while (1) __asm__ volatile("hlt");
        }
        if (current->is_free && current->next->is_free) {
            current->size += sizeof(heap_block_t) + sizeof(heap_footer_t) + current->next->size;
            current->next = current->next->next;
            if (current->next) current->next->prev = current;
            current->checksum = calculate_checksum(current);
            heap_footer_t* footer = (heap_footer_t*)((char*)current + sizeof(heap_block_t) + current->size);
            footer->magic_footer = HEAP_MAGIC_FOOTER;
            footer->checksum = current->checksum;
        } else {
            current = current->next;
        }
    }
}

void* kmalloc_debug(size_t size, const char* file, uint32_t line) {
    if (!size || size > MAX_ALLOC_SIZE) {
        serial_write("[HEAP] Invalid size\n");
        return NULL;
    }
    if (!heap_start) {
        serial_write("[HEAP] Not initialized\n");
        return NULL;
    }
    irq_state_t state = spinlock_acquire_irqsave(&heap_lock);
    size = (size + 15) & ~15;
    heap_block_t* block = find_free_block(size);
    if (!block) {
        serial_write("[HEAP] OOM (requested 0x");
        serial_write_hex(size);
        serial_write(")\n");
        spinlock_release_irqrestore(&heap_lock, state);
        return NULL;
    }
    split_block(block, size);
    block->is_free = false;
    block->file = file;
    block->line = line;
    block->timestamp = heap_alloc_count;
    block->checksum = calculate_checksum(block);
    heap_footer_t* footer = (heap_footer_t*)((char*)block + sizeof(heap_block_t) + block->size);
    footer->magic_footer = HEAP_MAGIC_FOOTER;
    footer->checksum = block->checksum;
    heap_alloc_count++;
    total_allocated += size;
    spinlock_release_irqrestore(&heap_lock, state);
    void* result = (void*)((char*)block + sizeof(heap_block_t));
    for (size_t i = 0; i < size; i++) {
        ((char*)result)[i] = 0xCD;
    }
    return result;
}

void* kmalloc(size_t size) {
    return kmalloc_debug(size, "unknown", 0);
}

void kfree(void* ptr) {
    if (!ptr) return;
    irq_state_t state = spinlock_acquire_irqsave(&heap_lock);
    heap_block_t* block = (heap_block_t*)((char*)ptr - sizeof(heap_block_t));
    if (!validate_block(block)) {
        spinlock_release_irqrestore(&heap_lock, state);
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_write("[HEAP] PANIC: Invalid free - corrupted!\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        while (1) __asm__ volatile("hlt");
    }
    if (block->is_free) {
        spinlock_release_irqrestore(&heap_lock, state);
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_write("[HEAP] PANIC: Double free!\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        while (1) __asm__ volatile("hlt");
    }
    for (size_t i = 0; i < block->size; i++) {
        ((char*)ptr)[i] = 0xDD;
    }
    block->is_free = true;
    heap_free_count++;
    total_allocated -= block->size;
    coalesce_free_blocks();
    spinlock_release_irqrestore(&heap_lock, state);
}

void* kcalloc(size_t num, size_t size) {
    if (!num || !size) return NULL;
    if (num > MAX_ALLOC_SIZE / size) {
        serial_write("[HEAP] kcalloc overflow\n");
        return NULL;
    }
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (!ptr) return NULL;
    for (size_t i = 0; i < total; i++) {
        ((char*)ptr)[i] = 0;
    }
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return NULL; }
    if (size > MAX_ALLOC_SIZE) return NULL;
    irq_state_t state = spinlock_acquire_irqsave(&heap_lock);
    heap_block_t* block = (heap_block_t*)((char*)ptr - sizeof(heap_block_t));
    if (!validate_block(block)) {
        spinlock_release_irqrestore(&heap_lock, state);
        return NULL;
    }
    if (block->size >= size) {
        spinlock_release_irqrestore(&heap_lock, state);
        return ptr;
    }
    spinlock_release_irqrestore(&heap_lock, state);
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    for (size_t i = 0; i < block->size && i < size; i++) {
        ((char*)new_ptr)[i] = ((char*)ptr)[i];
    }
    kfree(ptr);
    return new_ptr;
}

void heap_print_stats(void) {
    heap_block_t* current = heap_start;
    size_t free_blocks = 0, used_blocks = 0;
    size_t free_mem = 0, used_mem = 0;
    size_t corrupt = 0;
    while (current) {
        if (!validate_block(current)) {
            corrupt++;
            current = current->next;
            continue;
        }
        if (current->is_free) {
            free_blocks++;
            free_mem += current->size;
        } else {
            used_blocks++;
            used_mem += current->size;
        }
        current = current->next;
    }
    char buf[32];
    vga_write("\nHeap Stats:\n");
    vga_write("  Allocs: ");
    uitoa_local(heap_alloc_count, buf);
    vga_write(buf);
    vga_write(" Frees: ");
    uitoa_local(heap_free_count, buf);
    vga_write(buf);
    vga_write("\n  Free blocks: ");
    uitoa_local(free_blocks, buf);
    vga_write(buf);
    vga_write(" Used: ");
    uitoa_local(used_blocks, buf);
    vga_write(buf);
    if (corrupt) {
        vga_write(" CORRUPT: ");
        uitoa_local(corrupt, buf);
        vga_write(buf);
    }
    vga_write("\n  Free KB: ");
    uitoa_local(free_mem / 1024, buf);
    vga_write(buf);
    vga_write(" Used KB: ");
    uitoa_local(used_mem / 1024, buf);
    vga_write(buf);
    vga_write("\n\n");
}