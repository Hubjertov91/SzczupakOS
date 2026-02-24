#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>
#include <mm/vmm.h>

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_WAITING,
    TASK_ZOMBIE,
    TASK_TERMINATED
} task_state_t;

typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t kernel_rsp;
    uint64_t cr3;
} __attribute__((packed)) cpu_context_t;

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
    uint64_t syscall_kernel_rsp;
    bool kernel_preempt_ok;
} task_t;

bool task_init(void);
task_t* task_create(const char* name, void (*entry_point)(void));
task_t* task_create_user(const char* name, uint8_t* elf_data, size_t size);
task_t* task_fork(void);
void task_exit(void);
task_t* get_current_task(void);
void task_yield(void);
uint32_t task_get_pid(void);
void task_set_current(task_t* task);

#endif
