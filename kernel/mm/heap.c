#include <kernel/serial.h>
#include <kernel/vga.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>

#define HEAP_MAGIC 0xDEADBEEF
#define HEAP_INITIAL_SIZE (1024 * 1024)

typedef struct heap_block {
    uint32_t magic;
    size_t size;
    bool is_free;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* heap_start = NULL;

static void* sbrk(size_t size) {
    size_t pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void* addr = NULL;

    for (size_t i = 0; i < pages_needed; i++) {
        uint64_t page = pmm_alloc_page();
        if (page == 0) {
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
        vga_write("[HEAP] ERROR: Failed to initialize heap!\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    heap_start->magic = HEAP_MAGIC;
    heap_start->size = HEAP_INITIAL_SIZE - sizeof(heap_block_t);
    heap_start->is_free = true;
    heap_start->next = NULL;

    serial_write("[HEAP] Initialized\n");
}

static heap_block_t* find_free_block(size_t size) {
    heap_block_t* current = heap_start;
    while (current) {
        if (current->is_free && current->size >= size)
            return current;
        current = current->next;
    }
    return NULL;
}

static void split_block(heap_block_t* block, size_t size) {
    if (block->size <= size + sizeof(heap_block_t)) return;

    heap_block_t* new_block = (heap_block_t*)((char*)block + sizeof(heap_block_t) + size);
    new_block->magic = HEAP_MAGIC;
    new_block->size = block->size - size - sizeof(heap_block_t);
    new_block->is_free = true;
    new_block->next = block->next;

    block->size = size;
    block->next = new_block;
}

static void coalesce_free_blocks(void) {
    heap_block_t* current = heap_start;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            current->size += sizeof(heap_block_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

void* kmalloc(size_t size) {
    if (!size) return NULL;
    size = (size + 7) & ~7;

    heap_block_t* block = find_free_block(size);
    if (!block) {
        serial_write("[HEAP] Out of memory\n");
        return NULL;
    }

    split_block(block, size);
    block->is_free = false;

    return (void*)((char*)block + sizeof(heap_block_t));
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((char*)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) {
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_write("[HEAP] PANIC: Invalid free!\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        for (;;) {}
    }
    if (block->is_free) {
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_write("[HEAP] PANIC: Double free detected!\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        for (;;) {}
    }

    block->is_free = true;
    coalesce_free_blocks();
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (!ptr) return NULL;

    for (size_t i = 0; i < total; i++)
        ((char*)ptr)[i] = 0;
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return NULL; }

    heap_block_t* block = (heap_block_t*)((char*)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) return NULL;

    if (block->size >= size) return ptr;

    void* new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;

    for (size_t i = 0; i < block->size && i < size; i++)
        ((char*)new_ptr)[i] = ((char*)ptr)[i];

    kfree(ptr);
    return new_ptr;
}

void heap_print_stats(void) {
    heap_block_t* current = heap_start;
    size_t free_blocks = 0, used_blocks = 0;
    size_t free_mem = 0, used_mem = 0;

    while (current) {
        if (current->is_free) { free_blocks++; free_mem += current->size; }
        else { used_blocks++; used_mem += current->size; }
        current = current->next;
    }

    char buf[32];
    int idx;

    vga_write("\nHeap Info:\n");

    vga_write("  Free blocks: ");
    idx = 0;
    size_t val = free_blocks;
    if (val == 0) buf[idx++] = '0';
    else {
        char tmp[32]; int t = 0;
        while (val) { tmp[t++] = '0' + (val % 10); val /= 10; }
        for (int i = t - 1; i >= 0; i--) buf[idx++] = tmp[i];
    }
    buf[idx] = '\0';
    vga_write(buf);
    vga_write("\n");

    vga_write("  Used blocks: ");
    idx = 0; val = used_blocks;
    if (val == 0) buf[idx++] = '0';
    else {
        char tmp[32]; int t = 0;
        while (val) { tmp[t++] = '0' + (val % 10); val /= 10; }
        for (int i = t - 1; i >= 0; i--) buf[idx++] = tmp[i];
    }
    buf[idx] = '\0';
    vga_write(buf);
    vga_write("\n");

    vga_write("  Free KB: ");
    idx = 0; val = free_mem / 1024;
    if (val == 0) buf[idx++] = '0';
    else {
        char tmp[32]; int t = 0;
        while (val) { tmp[t++] = '0' + (val % 10); val /= 10; }
        for (int i = t - 1; i >= 0; i--) buf[idx++] = tmp[i];
    }
    buf[idx] = '\0';
    vga_write(buf);
    vga_write("\n");

    vga_write("  Used KB: ");
    idx = 0; val = used_mem / 1024;
    if (val == 0) buf[idx++] = '0';
    else {
        char tmp[32]; int t = 0;
        while (val) { tmp[t++] = '0' + (val % 10); val /= 10; }
        for (int i = t - 1; i >= 0; i--) buf[idx++] = tmp[i];
    }
    buf[idx] = '\0';
    vga_write(buf);
    vga_write("\n\n");
}

