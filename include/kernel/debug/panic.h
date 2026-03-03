#ifndef _KERNEL_DEBUG_PANIC_H
#define _KERNEL_DEBUG_PANIC_H

#include <kernel/stdint.h>

typedef struct {
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t cr2;
} panic_context_t;

const char* panic_exception_name(uint64_t vector);
void panic_print_backtrace(uint64_t rbp, uint64_t rip);
void panic_dump_and_halt(const char* reason, const panic_context_t* ctx) __attribute__((noreturn));
void panic_halt_message(const char* reason) __attribute__((noreturn));

#endif
