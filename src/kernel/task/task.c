#include <task/task.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <task/scheduler.h>
#include <drivers/serial.h>
#include <kernel/elf.h>
#include <kernel/stdint.h>
#include <net/net.h>
#include <kernel/pty.h>
#include <kernel/spinlock.h>

#define KERNEL_STACK_SIZE 16384
#define USER_STACK_SIZE (32 * 4096)
#define USER_STACK_TOP  0x0000008002000000ULL
#define USER_ARG_MAX 32
#define USER_CMDLINE_MAX 512
#define USER_ARG_REGION_GAP 4096

static task_t* current_task = NULL;
static task_t* task_list_head = NULL;
static uint32_t next_pid = 1;
static spinlock_t task_list_lock = SPINLOCK_INIT;

task_t* get_current_task(void) {
    return current_task;
}

extern void scheduler_add_task(task_t* task);
extern void scheduler_remove_task(task_t* task);
extern void schedule(void);
extern uint64_t pit_get_ticks(void);
extern void usermode_entry(void);

static bool task_stack_uses_vmm(void* stack, uint64_t stack_size) {
    uint64_t addr = (uint64_t)stack;
    if (!stack) return false;
    if (addr < 0xFFFF800000000000ULL) return false;
    if (stack_size == 0 || (stack_size % PAGE_SIZE) != 0) return false;
    return true;
}

static void task_list_insert(task_t* task) {
    if (!task) return;
    irq_state_t state = spinlock_acquire_irqsave(&task_list_lock);
    task->all_next = task_list_head;
    task_list_head = task;
    spinlock_release_irqrestore(&task_list_lock, state);
}

static bool task_list_remove(task_t* task) {
    if (!task) return false;
    irq_state_t state = spinlock_acquire_irqsave(&task_list_lock);

    task_t* prev = NULL;
    task_t* it = task_list_head;
    while (it) {
        if (it == task) {
            if (prev) prev->all_next = it->all_next;
            else task_list_head = it->all_next;
            it->all_next = NULL;
            spinlock_release_irqrestore(&task_list_lock, state);
            return true;
        }
        prev = it;
        it = it->all_next;
    }

    spinlock_release_irqrestore(&task_list_lock, state);
    return false;
}

static void task_detach_children_locked(uint32_t parent_pid) {
    for (task_t* it = task_list_head; it; it = it->all_next) {
        if (it->parent_pid != parent_pid) {
            continue;
        }
        it->parent_pid = 0;
        it->reap_blocked = false;
    }
}

static void task_destroy(task_t* task) {
    if (!task) return;

    if (task->page_dir) {
        vmm_destroy_address_space(task->page_dir);
        task->page_dir = NULL;
        task->cr3_phys = 0;
    }

    if (task->kernel_stack) {
        if (task_stack_uses_vmm(task->kernel_stack, task->stack_size)) {
            vmm_free_pages(task->kernel_stack, task->stack_size / PAGE_SIZE);
        } else {
            kfree(task->kernel_stack);
        }
        task->kernel_stack = NULL;
        task->stack_size = 0;
    }

    kfree(task);
}

static void idle_task_entry(void) {
    for (;;) {
        task_reap_terminated();
        __asm__ volatile("sti; hlt; cli");
        net_poll();
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

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool task_write_user(page_directory_t* dir, uint64_t user_addr, const void* src, size_t len) {
    if (!dir || !src || len == 0) return false;

    const uint8_t* in = (const uint8_t*)src;
    size_t remaining = len;
    uint64_t cur = user_addr;

    while (remaining > 0) {
        uint64_t phys = vmm_get_physical(dir, cur);
        if (!phys) return false;

        size_t page_off = (size_t)(cur & 0xFFFULL);
        size_t chunk = 4096 - page_off;
        if (chunk > remaining) chunk = remaining;

        uint8_t* dst = (uint8_t*)PHYS_TO_VIRT(phys & ~0xFFFULL) + page_off;
        memcpy(dst, in, chunk);

        in += chunk;
        cur += chunk;
        remaining -= chunk;
    }

    return true;
}

static bool task_write_user_u64(page_directory_t* dir, uint64_t user_addr, uint64_t value) {
    return task_write_user(dir, user_addr, &value, sizeof(value));
}

static bool task_build_user_args(task_t* task,
                                 const char* fallback_name,
                                 const char* cmdline,
                                 uint64_t stack_top,
                                 uint64_t* out_rsp,
                                 uint64_t* out_argc,
                                 uint64_t* out_argv) {
    if (!task || !task->page_dir || !fallback_name || !out_rsp || !out_argc || !out_argv) {
        return false;
    }

    const char* source = (cmdline && cmdline[0]) ? cmdline : fallback_name;

    char cmd_buf[USER_CMDLINE_MAX];
    size_t src_len = strlen(source);
    if (src_len >= sizeof(cmd_buf)) src_len = sizeof(cmd_buf) - 1;
    memcpy(cmd_buf, source, src_len);
    cmd_buf[src_len] = '\0';

    char* args[USER_ARG_MAX];
    size_t argc = 0;
    char* p = cmd_buf;
    while (*p != '\0' && argc < USER_ARG_MAX) {
        while (is_space(*p)) p++;
        if (*p == '\0') break;

        args[argc++] = p;
        while (*p != '\0' && !is_space(*p)) p++;
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }

    if (argc == 0) {
        size_t fallback_len = strlen(fallback_name);
        if (fallback_len >= sizeof(cmd_buf)) fallback_len = sizeof(cmd_buf) - 1;
        memcpy(cmd_buf, fallback_name, fallback_len);
        cmd_buf[fallback_len] = '\0';
        args[0] = cmd_buf;
        argc = 1;
    }

    uint64_t stack_bottom = stack_top - USER_STACK_SIZE;
    uint64_t sp = (stack_top - USER_ARG_REGION_GAP) & ~0xFULL;
    if (sp <= stack_bottom) return false;
    uint64_t user_arg_ptrs[USER_ARG_MAX];

    for (size_t i = argc; i > 0; i--) {
        const char* arg = args[i - 1];
        size_t arg_len = strlen(arg) + 1;
        if (sp < stack_bottom + arg_len) return false;

        sp -= arg_len;
        if (!task_write_user(task->page_dir, sp, arg, arg_len)) return false;
        user_arg_ptrs[i - 1] = sp;
    }

    sp &= ~0x7ULL;

    uint64_t table_size = (uint64_t)(argc + 2) * sizeof(uint64_t);
    if (((sp - table_size) & 0xFULL) != 0) {
        if (sp < stack_bottom + sizeof(uint64_t)) return false;
        sp -= sizeof(uint64_t);
    }

    if (sp < stack_bottom + table_size) return false;
    uint64_t table_base = sp - table_size;

    if (!task_write_user_u64(task->page_dir, table_base, (uint64_t)argc)) return false;
    for (size_t i = 0; i < argc; i++) {
        if (!task_write_user_u64(task->page_dir, table_base + sizeof(uint64_t) * (i + 1), user_arg_ptrs[i])) {
            return false;
        }
    }
    if (!task_write_user_u64(task->page_dir, table_base + sizeof(uint64_t) * (argc + 1), 0)) return false;

    *out_rsp = table_base;
    *out_argc = (uint64_t)argc;
    *out_argv = table_base + sizeof(uint64_t);
    return true;
}

bool task_init(void) {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) {
        serial_write("[TASK] ERROR: Failed to allocate idle task\n");
        return false;
    }

    current_task->pid = 0;
    current_task->parent_pid = 0;
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
    current_task->all_next = NULL;
    current_task->time_slice = 10;
    current_task->priority = 255;
    current_task->is_kernel = true;
    current_task->page_dir = NULL;
    current_task->cr3_phys = 0;
    current_task->cpu_time = 0;
    current_task->creation_time = 0;
    current_task->exit_code = 0;
    current_task->kernel_preempt_ok = false;
    current_task->pty_id = -1;
    current_task->reap_blocked = false;

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

    task_list_insert(current_task);
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
    task->parent_pid = current_task ? current_task->pid : 0;
    strcpy_safe(task->name, name, sizeof(task->name));
    task->state = TASK_READY;
    task->time_slice = 10;
    task->priority = 5;
    task->is_kernel = true;
    task->page_dir = NULL;
    task->cr3_phys = 0;
    task->cpu_time = 0;
    task->creation_time = pit_get_ticks();
    task->exit_code = 0;
    task->kernel_preempt_ok = false;
    task->pty_id = -1;
    task->reap_blocked = false;

    task->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack) {
        kfree(task);
        return NULL;
    }

    task->stack_size = KERNEL_STACK_SIZE;
    task->user_stack = NULL;
    task->next = NULL;
    task->all_next = NULL;

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

    task_list_insert(task);
    scheduler_add_task(task);
    return task;
}

task_t* task_create_user(const char* name, const char* cmdline, uint8_t* elf_data, size_t elf_size) {
    if (!name || !elf_data || !elf_size) return NULL;

    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return NULL;

    task->pid = next_pid++;
    task->parent_pid = current_task ? current_task->pid : 0;
    strcpy_safe(task->name, name, sizeof(task->name));
    task->state = TASK_READY;
    task->is_kernel = false;
    task->next = NULL;
    task->all_next = NULL;
    task->time_slice = 10;
    task->priority = 10;
    task->cpu_time = 0;
    task->creation_time = pit_get_ticks();
    task->exit_code = 0;
    task->kernel_preempt_ok = false;
    task->pty_id = -1;
    task->reap_blocked = (task->parent_pid != 0u);

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

    uint64_t stack_top = USER_STACK_TOP;
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

    uint64_t user_rsp = 0;
    uint64_t user_argc = 0;
    uint64_t user_argv = 0;
    if (!task_build_user_args(task, name, cmdline, stack_top, &user_rsp, &user_argc, &user_argv)) {
        vmm_destroy_address_space(task->page_dir);
        vmm_free_pages(task->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
        kfree(task);
        return NULL;
    }

    uint64_t* kstack = (uint64_t*)((uint64_t)task->kernel_stack + KERNEL_STACK_SIZE);

    *(--kstack) = 0x23;
    *(--kstack) = user_rsp;
    *(--kstack) = 0x202;
    *(--kstack) = 0x2B;
    *(--kstack) = entry;

    *(--kstack) = 0;  
    *(--kstack) = 32; 

    for (int i = 0; i < 15; i++) {
        *(--kstack) = 0;
    }

    kstack[9] = user_argc;
    kstack[10] = user_argv;
    
    task->context.r15 = 0;
    task->context.r14 = 0;
    task->context.r13 = 0;
    task->context.r12 = 0;
    task->context.rbp = 0;
    task->context.rbx = 0;
    task->context.kernel_rsp = (uint64_t)kstack;
    task->context.cr3 = task->cr3_phys;
    
    task->syscall_kernel_rsp = (uint64_t)task->kernel_stack + KERNEL_STACK_SIZE - 256;

    task_list_insert(task);
    scheduler_add_task(task);

    return task;
}

void task_exit(int32_t code) {
    if (!current_task) return;

    irq_state_t state = spinlock_acquire_irqsave(&task_list_lock);
    task_detach_children_locked(current_task->pid);
    spinlock_release_irqrestore(&task_list_lock, state);

    pty_detach_slave(current_task->pid);
    current_task->pty_id = -1;
    current_task->exit_code = code;
    current_task->reap_blocked = (current_task->parent_pid != 0u);
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

int32_t task_wait_pid(int32_t child_pid, int32_t* out_exit_code) {
    task_t* parent = get_current_task();
    if (!parent || parent->pid == 0) {
        return -1;
    }
    if (child_pid == 0 || child_pid < -1) {
        return -1;
    }

    bool restore_preempt = false;
    if (!parent->is_kernel && !parent->kernel_preempt_ok) {
        parent->kernel_preempt_ok = true;
        restore_preempt = true;
    }

    for (;;) {
        bool has_child = false;
        bool target_exists = false;
        task_t* exited_child = NULL;

        irq_state_t state = spinlock_acquire_irqsave(&task_list_lock);
        for (task_t* it = task_list_head; it; it = it->all_next) {
            if (it->parent_pid != parent->pid) {
                continue;
            }

            has_child = true;

            if (child_pid != -1 && it->pid != (uint32_t)child_pid) {
                continue;
            }

            target_exists = true;
            if (it->state == TASK_TERMINATED) {
                exited_child = it;
                break;
            }
        }

        if (exited_child) {
            int32_t exit_code = exited_child->exit_code;
            int32_t exited_pid = (int32_t)exited_child->pid;
            exited_child->reap_blocked = false;
            spinlock_release_irqrestore(&task_list_lock, state);

            task_reap_terminated();
            if (out_exit_code) {
                *out_exit_code = exit_code;
            }

            if (restore_preempt) {
                parent->kernel_preempt_ok = false;
            }
            return exited_pid;
        }

        spinlock_release_irqrestore(&task_list_lock, state);

        if (!has_child || (child_pid != -1 && !target_exists)) {
            if (restore_preempt) {
                parent->kernel_preempt_ok = false;
            }
            return -1;
        }

        __asm__ volatile("sti; hlt; cli");
        net_poll();
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

uint32_t task_get_process_count(void) {
    uint32_t count = 0;
    irq_state_t state = spinlock_acquire_irqsave(&task_list_lock);
    for (task_t* it = task_list_head; it; it = it->all_next) {
        if (it->state != TASK_TERMINATED) {
            count++;
        }
    }
    spinlock_release_irqrestore(&task_list_lock, state);
    return count;
}

void task_reap_terminated(void) {
    for (;;) {
        task_t* victim = NULL;

        irq_state_t state = spinlock_acquire_irqsave(&task_list_lock);
        task_t* it = task_list_head;
        while (it) {
            if (it != current_task && it->state == TASK_TERMINATED) {
                if (it->reap_blocked) {
                    it = it->all_next;
                    continue;
                }
                victim = it;
                break;
            }
            it = it->all_next;
        }
        spinlock_release_irqrestore(&task_list_lock, state);

        if (!victim) {
            return;
        }
        if (!task_list_remove(victim)) {
            continue;
        }

        pty_detach_slave(victim->pid);
        task_destroy(victim);
    }
}

void task_set_current(task_t* task) {
    current_task = task;
}

task_t* task_fork(void) {
    serial_write("[TASK] fork is temporarily disabled (unsafe semantics)\n");
    return NULL;
}
