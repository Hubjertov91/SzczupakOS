#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include "stdint.h"
#include "vmm.h"

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cs, ss;
    uint64_t _pad[18];
    uint64_t cr3;
} cpu_context_t;

typedef struct task {
    uint32_t pid;
    char name[64];
    task_state_t state;
    cpu_context_t context;
    void* kernel_stack;
    uint64_t stack_size;
    struct task* next;
    uint64_t time_slice;
    uint8_t priority;
    bool is_kernel;
    page_directory_t* page_dir;
    void* user_stack;
    uint64_t cr3_phys;
    uint64_t cpu_time;
    uint64_t creation_time;
} task_t;

void task_init(void);
task_t* task_create(const char* name, void (*entry_point)(void));
task_t* task_create_user(const char* name, uint8_t* elf_data, size_t size);
void task_exit(void);
void task_yield(void);
task_t* task_get_current(void);
uint32_t task_get_pid(void);
void task_set_current(task_t* task);

#endif