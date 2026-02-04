#include <kernel/task.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/elf.h>

#define KERNEL_STACK_SIZE 8192
#define USER_STACK_SIZE (4 * 4096)

static task_t* current_task = NULL;
static task_t* task_list_head = NULL;
static uint32_t next_pid = 1;

extern void scheduler_add_task(task_t* task);
extern void scheduler_remove_task(task_t* task);
extern void schedule(void);
extern uint64_t pit_get_ticks(void);

static void strcpy_safe(char* dst, const char* src, size_t max) {
    if (!dst || !src || max == 0) return;
    
    size_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void task_init(void) {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) {
        serial_write("[TASK] ERROR: Failed to allocate idle task\n");
        return;
    }
    
    current_task->pid = 0;
    strcpy_safe(current_task->name, "idle", sizeof(current_task->name));
    current_task->state = TASK_RUNNING;
    current_task->kernel_stack = NULL;
    current_task->user_stack = NULL;
    current_task->stack_size = 0;
    current_task->next = NULL;
    current_task->time_slice = 10;
    current_task->priority = 0;
    current_task->is_kernel = true;
    current_task->page_dir = NULL;
    current_task->cr3_phys = 0;
    current_task->cpu_time = 0;
    current_task->creation_time = 0;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    current_task->context.cr3 = cr3;
    current_task->cr3_phys = cr3;

    task_list_head = current_task;
    serial_write("[TASK] Idle task initialized\n");
}

task_t* task_create(const char* name, void (*entry_point)(void)) {
    if (!name || !entry_point) {
        serial_write("[TASK] ERROR: Invalid parameters to task_create\n");
        return NULL;
    }
    
    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) {
        serial_write("[TASK] ERROR: Failed to allocate task structure\n");
        return NULL;
    }
    
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
        serial_write("[TASK] ERROR: Failed to allocate kernel stack\n");
        kfree(task);
        return NULL;
    }
    
    task->stack_size = KERNEL_STACK_SIZE;
    task->user_stack = NULL;
    task->next = NULL;

    task->context.rax = 0;
    task->context.rbx = 0;
    task->context.rcx = 0;
    task->context.rdx = 0;
    task->context.rsi = 0;
    task->context.rdi = 0;
    task->context.rbp = 0;
    task->context.r8  = 0;
    task->context.r9  = 0;
    task->context.r10 = 0;
    task->context.r11 = 0;
    task->context.r12 = 0;
    task->context.r13 = 0;
    task->context.r14 = 0;
    task->context.r15 = 0;
    task->context.rip    = (uint64_t)entry_point;
    task->context.rflags = 0x202;
    task->context.cs     = 0x08;
    task->context.ss     = 0x10;
    task->context.rsp    = (uint64_t)task->kernel_stack + KERNEL_STACK_SIZE;
    task->context.cr3    = 0;

    scheduler_add_task(task);
    return task;
}

task_t* task_create_user(const char* name, uint8_t* elf_data, size_t elf_size) {
    if (!name || !elf_data || !elf_size) {
        serial_write("[TASK] ERROR: Invalid parameters to task_create_user\n");
        return NULL;
    }
    
    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) {
        serial_write("[TASK] ERROR: Failed to allocate user task structure\n");
        return NULL;
    }

    task->pid = next_pid++;
    strcpy_safe(task->name, name, sizeof(task->name));
    task->state = TASK_READY;
    task->is_kernel = false;
    task->next = NULL;
    task->time_slice = 10;
    task->priority = 10;
    task->cpu_time = 0;
    task->creation_time = pit_get_ticks();

    task->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack) {
        serial_write("[TASK] ERROR: Failed to allocate kernel stack for user task\n");
        kfree(task);
        return NULL;
    }

    task->page_dir = vmm_create_address_space();
    if (!task->page_dir) {
        serial_write("[TASK] ERROR: Failed to create user address space\n");
        kfree(task->kernel_stack);
        kfree(task);
        return NULL;
    }
    task->cr3_phys = task->page_dir->pml4_phys;

    serial_write("[TASK] CR3 physical: 0x");
    serial_write_hex(task->cr3_phys);
    serial_write("\n");

    uint64_t entry = elf_load(task, elf_data, elf_size);
    if (!entry) {
        serial_write("[TASK] ELF loading failed\n");
        kfree(task->kernel_stack);
        kfree(task);
        return NULL;
    }

    serial_write("[TASK] Entry point: 0x");
    serial_write_hex(entry);
    serial_write("\n");

    uint64_t stack_top = 0x20000000;
    task->user_stack = (void*)stack_top;
    task->stack_size = USER_STACK_SIZE;

    for (uint64_t offset = 0; offset < USER_STACK_SIZE; offset += 4096) {
        uint64_t page_vaddr = stack_top - USER_STACK_SIZE + offset;
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            serial_write("[TASK] Failed to allocate stack page\n");
            kfree(task->kernel_stack);
            kfree(task);
            return NULL;
        }

        serial_write("[TASK] Mapping stack page 0x");
        serial_write_hex(page_vaddr);
        serial_write(" -> 0x");
        serial_write_hex(phys);
        serial_write("\n");

        if (!vmm_map_user_page(task->page_dir, page_vaddr, phys, PAGE_PRESENT | PAGE_WRITE)) {
            serial_write("[TASK] Failed to map stack page\n");
            kfree(task->kernel_stack);
            kfree(task);
            return NULL;
        }
    }

    task->context.rax = 0;
    task->context.rbx = 0;
    task->context.rcx = 0;
    task->context.rdx = 0;
    task->context.rsi = 0;
    task->context.rdi = 0;
    task->context.rbp = 0;
    task->context.r8  = 0;
    task->context.r9  = 0;
    task->context.r10 = 0;
    task->context.r11 = 0;
    task->context.r12 = 0;
    task->context.r13 = 0;
    task->context.r14 = 0;
    task->context.r15 = 0;
    task->context.rip    = entry;
    task->context.rsp    = stack_top;
    task->context.rflags = 0x202;
    task->context.cs     = 0x1B;
    task->context.ss     = 0x23;
    task->context.cr3    = task->cr3_phys;

    serial_write("[TASK] CS=0x");
    serial_write_hex(task->context.cs);
    serial_write(" SS=0x");
    serial_write_hex(task->context.ss);
    serial_write(" CR3=0x");
    serial_write_hex(task->context.cr3);
    serial_write("\n");

    scheduler_add_task(task);

    return task;
}

void task_exit(void) {
    if (!current_task) return;
    
    serial_write("[TASK] Task '");
    serial_write(current_task->name);
    serial_write("' exited\n");
    
    current_task->state = TASK_TERMINATED;
    scheduler_remove_task(current_task);
    
    extern page_directory_t* vmm_get_kernel_directory(void);
    page_directory_t* kernel_dir = vmm_get_kernel_directory();
    if (kernel_dir) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_dir->pml4_phys) : "memory");
    }
    
    serial_write("[TASK] No tasks, idle loop\n");
    __asm__ volatile("sti");
    while (1) {
        __asm__ volatile("hlt");
    }
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
    if (!current_task) {
        serial_write("[TASK] ERROR: No current task to fork\n");
        return NULL;
    }
    
    task_t* child = (task_t*)kmalloc(sizeof(task_t));
    if (!child) {
        serial_write("[TASK] ERROR: Failed to allocate child task\n");
        return NULL;
    }
    
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
        serial_write("[TASK] ERROR: Failed to allocate kernel stack for child\n");
        kfree(child);
        return NULL;
    }
    child->stack_size = KERNEL_STACK_SIZE;
    
    if (current_task->is_kernel) {
        child->user_stack = NULL;
        child->page_dir = NULL;
        child->cr3_phys = 0;
        __asm__ volatile("mov %%cr3, %0" : "=r"(child->context.cr3));
    } else {
        if (current_task->user_stack) {
            child->user_stack = kmalloc(USER_STACK_SIZE);
            if (!child->user_stack) {
                serial_write("[TASK] ERROR: Failed to allocate user stack for child\n");
                kfree(child->kernel_stack);
                kfree(child);
                return NULL;
            }
            for (size_t i = 0; i < USER_STACK_SIZE; i++) {
                ((char*)child->user_stack)[i] = ((char*)current_task->user_stack)[i];
            }
        } else {
            child->user_stack = NULL;
        }
        
        child->page_dir = current_task->page_dir;
        child->cr3_phys = current_task->cr3_phys;
        child->context.cr3 = current_task->context.cr3;
    }
    
    child->context.rax = 0;
    child->context.rbx = current_task->context.rbx;
    child->context.rcx = current_task->context.rcx;
    child->context.rdx = current_task->context.rdx;
    child->context.rsi = current_task->context.rsi;
    child->context.rdi = current_task->context.rdi;
    child->context.rbp = current_task->context.rbp;
    child->context.r8  = current_task->context.r8;
    child->context.r9  = current_task->context.r9;
    child->context.r10 = current_task->context.r10;
    child->context.r11 = current_task->context.r11;
    child->context.r12 = current_task->context.r12;
    child->context.r13 = current_task->context.r13;
    child->context.r14 = current_task->context.r14;
    child->context.r15 = current_task->context.r15;
    child->context.rip = current_task->context.rip;
    child->context.rflags = current_task->context.rflags;
    child->context.cs = current_task->context.cs;
    child->context.ss = current_task->context.ss;
    child->context.rsp = (uint64_t)child->kernel_stack + KERNEL_STACK_SIZE;
    
    scheduler_add_task(child);
    
    serial_write("[TASK] Forked task ");
    serial_write_dec(child->pid);
    serial_write(" from task ");
    serial_write_dec(current_task->pid);
    serial_write("\n");
    
    return child;
}