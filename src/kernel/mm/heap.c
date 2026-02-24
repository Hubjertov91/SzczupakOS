#include <drivers/serial.h>
#include <kernel/vga.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <kernel/spinlock.h>
#include <kernel/stdint.h>

#define HEAP_MAGIC 0xABCD1234
#define PAGE_SIZE 4096
#define HEAP_MIN_SIZE 16
#define HEAP_PAGES 32

typedef struct heap_block {
    uint32_t magic;
    size_t size;
    bool free;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

static heap_block_t* heap_start = NULL;
static spinlock_t heap_lock = SPINLOCK_INIT;
static uint8_t heap_storage[HEAP_PAGES * PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE), section(".bss.heap")));

bool heap_init(void) {
    heap_start = (heap_block_t*)heap_storage;
    heap_start->magic = HEAP_MAGIC;
    heap_start->size = (HEAP_PAGES * PAGE_SIZE) - sizeof(heap_block_t);
    heap_start->free = true;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    serial_write("[HEAP] Ready at 0x");
    serial_write_hex((uint64_t)heap_start);
    serial_write(" (");
    serial_write_dec(HEAP_PAGES * PAGE_SIZE / 1024);
    serial_write(" KB)\n");
    
    return true;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    irq_state_t state = spinlock_acquire_irqsave(&heap_lock);
    
    heap_block_t* current = heap_start;
    while (current) {
        if (current->magic != HEAP_MAGIC) {
            serial_write("[HEAP] CORRUPT\n");
            spinlock_release_irqrestore(&heap_lock, state);
            return NULL;
        }
        
        if (current->free && current->size >= size) {
            if (current->size > size + sizeof(heap_block_t) + HEAP_MIN_SIZE) {
                heap_block_t* new_block = (heap_block_t*)((char*)current + sizeof(heap_block_t) + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = current->size - size - sizeof(heap_block_t);
                new_block->free = true;
                new_block->next = current->next;
                new_block->prev = current;
                
                if (current->next) {
                    current->next->prev = new_block;
                }
                current->next = new_block;
                current->size = size;
            }
            
            current->free = false;
            spinlock_release_irqrestore(&heap_lock, state);
            return (void*)((char*)current + sizeof(heap_block_t));
        }
        
        current = current->next;
    }
    
    spinlock_release_irqrestore(&heap_lock, state);
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    irq_state_t state = spinlock_acquire_irqsave(&heap_lock);
    
    heap_block_t* block = (heap_block_t*)((char*)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) {
        serial_write("[HEAP] BAD FREE\n");
        spinlock_release_irqrestore(&heap_lock, state);
        return;
    }
    
    block->free = true;
    
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }
    
    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
    
    spinlock_release_irqrestore(&heap_lock, state);
}

void* kcalloc(size_t num, size_t size) {
    if (num == 0 || size == 0) return NULL;
    
    size_t total = num * size;
    if (total / num != size) {
        serial_write("[HEAP] ERROR: Size overflow in kcalloc\n");
        return NULL;
    }
    
    void* ptr = kmalloc(total);
    if (ptr) {
        char* p = (char*)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    heap_block_t* block = (heap_block_t*)((char*)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) {
        return NULL;
    }
    
    if (block->size >= size) {
        return ptr;
    }
    
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    
    for (size_t i = 0; i < block->size && i < size; i++) {
        ((char*)new_ptr)[i] = ((char*)ptr)[i];
    }
    
    kfree(ptr);
    return new_ptr;
}
