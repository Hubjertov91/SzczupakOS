#include <task/task.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <kernel/vga.h>
#include <task/scheduler.h>
#include <drivers/serial.h>
#include <kernel/elf.h>
#include <kernel/stdint.h>

#define KERNEL_STACK_SIZE 16384
#define USER_STACK_SIZE (32 * 4096)

static task_t* current_task = NULL;
static task_t* task_list_head = NULL;
static uint32_t next_pid = 1;

task_t* get_current_task(void) {
    return current_task;
}

extern void scheduler_add_task(task_t* task);
extern void scheduler_remove_task(task_t* task);
extern void schedule(void);
extern uint64_t pit_get_ticks(void);
extern void usermode_entry(void);

static void idle_task_entry(void) {
    for (;;) {
        __asm__ volatile("sti; hlt");
    }
}

static void strcpy_safe(char* dst, const char* src, size_t max) {
    if (!dst || !src || max == 0) return;
    size_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

bool task_init(void) {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) {
        serial_write("[TASK] ERROR: Failed to allocate idle task\n");
        return false;
    }

    current_task->pid = 0;
    strcpy_safe(current_task->name, "idle", sizeof(current_task->name));
    current_task->state = TASK_RUNNING;
    
    current_task->kernel_stack = vmm_alloc_pages(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!current_task->kernel_stack) {
        serial_write("[TASK] ERROR: Failed to allocate idle task kernel stack\n");
        kfree(current_task);
        return false;
    }
    current_task->stack_size = KERNEL_STACK_SIZE;
    current_task->user_stack = NULL;
    current_task->next = NULL;
    current_task->time_slice = 10;
    current_task->priority = 0;
    current_task->is_kernel = true;
    current_task->page_dir = NULL;
    current_task->cr3_phys = 0;
    current_task->cpu_time = 0;
    current_task->creation_time = 0;

    current_task->context.r15 = 0;
    current_task->context.r14 = 0;
    current_task->context.r13 = 0;
    current_task->context.r12 = 0;
    current_task->context.rbp = 0;
    current_task->context.rbx = 0;

    uint64_t* kstack = (uint64_t*)((uint64_t)current_task->kernel_stack + KERNEL_STACK_SIZE);

    *(--kstack) = 0x202;
    *(--kstack) = 0x08;
    *(--kstack) = (uint64_t)idle_task_entry;

    *(--kstack) = 0;
    *(--kstack) = 32;

    for (int i = 0; i < 15; i++) {
        *(--kstack) = 0;
    }

    current_task->context.kernel_rsp = (uint64_t)kstack;
    current_task->context.cr3 = 0;
    current_task->syscall_kernel_rsp = (uint64_t)current_task->kernel_stack + KERNEL_STACK_SIZE - 256;

    task_list_head = current_task;
    serial_write("[TASK] Idle task generated\n");
    return true;
}

task_t* task_create(const char* name, void (*entry_point)(void)) {
    if (!name || !entry_point) return NULL;

    if (next_pid >= 0xFFFFFFFF) {
        serial_write("[TASK] ERROR: PID overflow\n");
        return NULL;
    }

    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return NULL;

    task->pid = next_pid++;
    strcpy_safe(task->name, name, sizeof(task->name));
    task->state = TASK_READY;
    task->time_slice = 10;
    task->priority = 5;
    task->is_kernel = true;
    task->page_dir = NULL;
    task->cr3_phys = 0;
    task->cpu_time = 0;
    task->creation_time = pit_get_ticks();

    task->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack) {
        kfree(task);
        return NULL;
    }

    task->stack_size = KERNEL_STACK_SIZE;
    task->user_stack = NULL;
    task->next = NULL;

    uint64_t* kstack = (uint64_t*)((uint64_t)task->kernel_stack + KERNEL_STACK_SIZE);

    *(--kstack) = 0x202;                 
    *(--kstack) = 0x08;                  
    *(--kstack) = (uint64_t)entry_point; 

    *(--kstack) = 0;  
    *(--kstack) = 32; 

    for (int i = 0; i < 15; i++) {
        *(--kstack) = 0;
    }

    task->context.r15 = 0;
    task->context.r14 = 0;
    task->context.r13 = 0;
    task->context.r12 = 0;
    task->context.rbp = 0;
    task->context.rbx = 0;
    task->context.kernel_rsp = (uint64_t)kstack;
    task->context.cr3 = 0;
    task->syscall_kernel_rsp = (uint64_t)task->kernel_stack + KERNEL_STACK_SIZE - 256;

    scheduler_add_task(task);
    return task;
}

task_t* task_create_user(const char* name, uint8_t* elf_data, size_t elf_size) {
    if (!name || !elf_data || !elf_size) return NULL;

    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return NULL;

    task->pid = next_pid++;
    strcpy_safe(task->name, name, sizeof(task->name));
    task->state = TASK_READY;
    task->is_kernel = false;
    task->next = NULL;
    task->time_slice = 10;
    task->priority = 10;
    task->cpu_time = 0;
    task->creation_time = pit_get_ticks();

    task->kernel_stack = vmm_alloc_pages(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!task->kernel_stack) {
        kfree(task);
        return NULL;
    }
    task->stack_size = KERNEL_STACK_SIZE;

    task->page_dir = vmm_create_address_space();
    if (!task->page_dir) {
        vmm_free_pages(task->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
        kfree(task);
        return NULL;
    }
    task->cr3_phys = task->page_dir->pml4_phys;

    uint64_t entry = elf_load(task, elf_data, elf_size);
    if (!entry) {
        vmm_destroy_address_space(task->page_dir);
        vmm_free_pages(task->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
        kfree(task);
        return NULL;
    }

    uint64_t stack_top = 0x20000000;
    task->user_stack = (void*)stack_top;

    for (uint64_t offset = 0; offset < USER_STACK_SIZE; offset += 4096) {
        uint64_t page_vaddr = stack_top - USER_STACK_SIZE + offset;
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            vmm_destroy_address_space(task->page_dir);
            vmm_free_pages(task->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
            kfree(task);
            return NULL;
        }
        if (!vmm_map_user_page(task->page_dir, page_vaddr, phys, PAGE_PRESENT | PAGE_WRITE)) {
            vmm_destroy_address_space(task->page_dir);
            vmm_free_pages(task->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
            kfree(task);
            return NULL;
        }
    }

    uint64_t* kstack = (uint64_t*)((uint64_t)task->kernel_stack + KERNEL_STACK_SIZE);

    *(--kstack) = 0x23;                           
    *(--kstack) = (stack_top - 16) & ~0xF;        
    *(--kstack) = 0x202;                          
    *(--kstack) = 0x2B;                           
    *(--kstack) = entry;                          

    *(--kstack) = 0;  
    *(--kstack) = 32; 

    for (int i = 0; i < 15; i++) {
        *(--kstack) = 0;
    }
    
    task->context.r15 = 0;
    task->context.r14 = 0;
    task->context.r13 = 0;
    task->context.r12 = 0;
    task->context.rbp = (stack_top - 16) & ~0xF;
    task->context.rbx = 0;
    task->context.kernel_rsp = (uint64_t)kstack;
    task->context.cr3 = task->cr3_phys;
    
    task->syscall_kernel_rsp = (uint64_t)task->kernel_stack + KERNEL_STACK_SIZE - 256;

    scheduler_add_task(task);

    extern uint64_t syscall_kernel_rsp;
    syscall_kernel_rsp = task->syscall_kernel_rsp;
    
    return task;
}

void task_exit(void) {
    if (!current_task) return;

    serial_write("[TASK] Task exited\n");

    current_task->state = TASK_TERMINATED;
    scheduler_remove_task(current_task);

    extern page_directory_t* vmm_get_kernel_directory(void);
    page_directory_t* kernel_dir = vmm_get_kernel_directory();
    if (kernel_dir) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_dir->pml4_phys) : "memory");
    }

    __asm__ volatile("sti");
    while (1) __asm__ volatile("hlt");
}

void task_yield(void) {
    schedule();
}

task_t* task_get_current(void) {
    return current_task;
}

uint32_t task_get_pid(void) {
    return current_task ? current_task->pid : 0;
}

void task_set_current(task_t* task) {
    current_task = task;
}

task_t* task_fork(void) {
    if (!current_task) return NULL;

    task_t* child = (task_t*)kmalloc(sizeof(task_t));
    if (!child) return NULL;

    child->pid = next_pid++;
    strcpy_safe(child->name, current_task->name, sizeof(child->name));
    child->state = TASK_READY;
    child->time_slice = current_task->time_slice;
    child->priority = current_task->priority;
    child->is_kernel = current_task->is_kernel;
    child->cpu_time = 0;
    child->creation_time = pit_get_ticks();
    child->next = NULL;

    child->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!child->kernel_stack) {
        kfree(child);
        return NULL;
    }
    child->stack_size = KERNEL_STACK_SIZE;

    if (current_task->is_kernel) {
        child->user_stack = NULL;
        child->page_dir = NULL;
        child->cr3_phys = 0;
    } else {
        if (current_task->user_stack) {
            child->user_stack = kmalloc(USER_STACK_SIZE);
            if (!child->user_stack) {
                kfree(child->kernel_stack);
                kfree(child);
                return NULL;
            }
            for (size_t i = 0; i < USER_STACK_SIZE; i++)
                ((char*)child->user_stack)[i] = ((char*)current_task->user_stack)[i];
        } else {
            child->user_stack = NULL;
        }
        child->page_dir = current_task->page_dir;
        child->cr3_phys = current_task->cr3_phys;
    }

    child->context.r15 = 0;
    child->context.r14 = 0;
    child->context.r13 = 0;
    child->context.r12 = 0;
    child->context.rbp = 0;
    child->context.rbx = 0;
    child->context.kernel_rsp = (uint64_t)child->kernel_stack + KERNEL_STACK_SIZE;

    scheduler_add_task(child);
    return child;
}