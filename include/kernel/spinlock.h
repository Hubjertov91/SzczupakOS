#ifndef _KERNEL_SPINLOCK_H
#define _KERNEL_SPINLOCK_H

#include "stdint.h"

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spinlock_acquire(spinlock_t* lock) {
    if (!lock) return;
    while (__atomic_exchange_n(&lock->lock, 1, __ATOMIC_ACQUIRE) != 0) {
        __asm__ volatile("pause");
    }
}

static inline void spinlock_release(spinlock_t* lock) {
    if (!lock) return;
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}

static inline int spinlock_trylock(spinlock_t* lock) {
    if (!lock) return 0;
    return __atomic_exchange_n(&lock->lock, 1, __ATOMIC_ACQUIRE) == 0 ? 1 : 0;
}

typedef struct {
    uint64_t rflags;
    uint32_t irq_state;
} irq_state_t;

static inline irq_state_t spinlock_acquire_irqsave(spinlock_t* lock) {
    irq_state_t state;
    __asm__ volatile("pushfq; pop %0" : "=r"(state.rflags) : : "memory");
    __asm__ volatile("cli" : : : "memory");
    spinlock_acquire(lock);
    return state;
}

static inline void spinlock_release_irqrestore(spinlock_t* lock, irq_state_t state) {
    spinlock_release(lock);
    if (state.rflags & 0x200) {
        __asm__ volatile("sti" : : : "memory");
    }
}

#endif
