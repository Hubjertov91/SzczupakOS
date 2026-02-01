#include <kernel/task.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/elf.h>
#include <kernel/scheduler.h>

#define KERNEL_STACK_SIZE 8192
#define USER_STACK_SIZE (4 * 4096)

static task_t* current_task = NULL;
static task_t* task_list_head = NULL;
static uint32_t next_pid = 1;

extern void scheduler_add_task(task_t* task);
extern void scheduler_remove_task(task_t* task);
extern void schedule(void);

static void strcpy(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

void task_init(void) {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) return;
    current_task->pid = 0;
    strcpy(current_task->name, "idle");
    current_task->state = TASK_RUNNING;
    current_task->kernel_stack = NULL;
    current_task->user_stack = NULL;
    current_task->stack_size = 0;
    current_task->next = NULL;
    current_task->time_slice = 10;
    current_task->is_kernel = true;
    current_task->page_dir = NULL;
    current_task->cr3_phys = 0;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    current_task->context.cr3 = cr3;
    current_task->cr3_phys = cr3;

    task_list_head = current_task;
}

task_t* task_create(const char* name, void (*entry_point)(void)) {
    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return NULL;
    task->pid = next_pid++;
    strcpy(task->name, name);
    task->state = TASK_READY;
    task->time_slice = 10;
    task->is_kernel = true;
    task->page_dir = NULL;
    task->cr3_phys = 0;
    task->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack) { kfree(task); return NULL; }
    task->stack_size = KERNEL_STACK_SIZE;
    task->user_stack = NULL;
    task->next = NULL;

    task->context.rax = 0; task->context.rbx = 0;
    task->context.rcx = 0; task->context.rdx = 0;
    task->context.rsi = 0; task->context.rdi = 0;
    task->context.rbp = 0;
    task->context.r8  = 0; task->context.r9  = 0;
    task->context.r10 = 0; task->context.r11 = 0;
    task->context.r12 = 0; task->context.r13 = 0;
    task->context.r14 = 0; task->context.r15 = 0;
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
    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return NULL;

    task->pid = next_pid++;
    strcpy(task->name, name);
    task->state = TASK_READY;
    task->is_kernel = false;
    task->next = NULL;
    task->time_slice = 10;

    task->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack) {
        kfree(task);
        return NULL;
    }

    task->page_dir = vmm_create_address_space();
    if (!task->page_dir) {
        serial_write("[TASK] Failed to create address space\n");
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

    task->context.rax = 0; task->context.rbx = 0;
    task->context.rcx = 0; task->context.rdx = 0;
    task->context.rsi = 0; task->context.rdi = 0;
    task->context.rbp = 0;
    task->context.r8  = 0; task->context.r9  = 0;
    task->context.r10 = 0; task->context.r11 = 0;
    task->context.r12 = 0; task->context.r13 = 0;
    task->context.r14 = 0; task->context.r15 = 0;
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