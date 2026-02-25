#include <drivers/serial.h>
#include <kernel/vga.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <kernel/spinlock.h>
#include <kernel/stdint.h>

#define HEAP_MAGIC 0xABCD1234
#define PAGE_SIZE 4096
#define HEAP_MIN_SIZE 16
#define HEAP_PAGES 256
#define HEAP_MAX_STEPS 65536

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

static bool heap_block_ptr_valid(const heap_block_t* block) {
    if (!block) return false;
    uint64_t start = (uint64_t)heap_storage;
    uint64_t end = start + (HEAP_PAGES * PAGE_SIZE);
    uint64_t p = (uint64_t)block;
    return p >= start && (p + sizeof(heap_block_t)) <= end;
}

static void heap_merge_next(heap_block_t* block) {
    if (!heap_block_ptr_valid(block)) return;

    heap_block_t* next = block->next;
    if (!heap_block_ptr_valid(next)) return;
    if (next->magic != HEAP_MAGIC || !next->free) return;

    uint64_t block_end = (uint64_t)block + sizeof(heap_block_t) + block->size;
    if (block_end != (uint64_t)next) return;

    block->size += sizeof(heap_block_t) + next->size;
    block->next = next->next;
    if (heap_block_ptr_valid(next->next)) {
        next->next->prev = block;
    }
}

static bool heap_lock_acquire(irq_state_t* state_out) {
    if (!state_out) return false;
    __asm__ volatile("pushfq; pop %0" : "=r"(state_out->rflags) : : "memory");
    __asm__ volatile("cli" : : : "memory");
    uint32_t spins = 0;
    while (!spinlock_trylock(&heap_lock)) {
        if (++spins > 1000000) {
            serial_write("[HEAP] LOCK TIMEOUT\n");
            if (state_out->rflags & 0x200) {
                __asm__ volatile("sti" : : : "memory");
            }
            return false;
        }
        __asm__ volatile("pause");
    }
    return true;
}

static void heap_lock_release(irq_state_t state) {
    spinlock_release_irqrestore(&heap_lock, state);
}

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
    if (size < HEAP_MIN_SIZE) size = HEAP_MIN_SIZE;
    size = (size + 7) & ~((size_t)7);
    
    irq_state_t state;
    if (!heap_lock_acquire(&state)) return NULL;
    
    heap_block_t* current = heap_start;
    size_t steps = 0;
    while (current) {
        if (++steps > HEAP_MAX_STEPS) {
            serial_write("[HEAP] LOOP DETECTED\n");
            heap_lock_release(state);
            return NULL;
        }
        if (!heap_block_ptr_valid(current)) {
            serial_write("[HEAP] BAD PTR\n");
            heap_lock_release(state);
            return NULL;
        }
        if (current->magic != HEAP_MAGIC) {
            serial_write("[HEAP] CORRUPT\n");
            heap_lock_release(state);
            return NULL;
        }

        if (current->free) {
            heap_merge_next(current);
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
            heap_lock_release(state);
            return (void*)((char*)current + sizeof(heap_block_t));
        }
        
        if (current->next && !heap_block_ptr_valid(current->next)) {
            serial_write("[HEAP] BAD NEXT PTR\n");
            heap_lock_release(state);
            return NULL;
        }
        current = current->next;
    }
    
    heap_lock_release(state);
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    irq_state_t state;
    if (!heap_lock_acquire(&state)) return;
    
    heap_block_t* block = (heap_block_t*)((char*)ptr - sizeof(heap_block_t));
    if (!heap_block_ptr_valid(block)) {
        serial_write("[HEAP] BAD FREE PTR\n");
        heap_lock_release(state);
        return;
    }
    if (block->magic != HEAP_MAGIC) {
        serial_write("[HEAP] BAD FREE\n");
        heap_lock_release(state);
        return;
    }
    if (block->free) {
        serial_write("[HEAP] DOUBLE FREE\n");
        heap_lock_release(state);
        return;
    }
    
    block->free = true;
    heap_merge_next(block);
    if (heap_block_ptr_valid(block->prev) && block->prev->magic == HEAP_MAGIC && block->prev->free) {
        heap_merge_next(block->prev);
    }
    
    heap_lock_release(state);
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
