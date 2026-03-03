#include <task/scheduler.h>
#include <kernel/task/task.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/drivers/serial.h>
#include <debug/panic.h>
#include <arch/api.h>
#include <mm/vmm.h>

#define KERNEL_STACK_SIZE 16384

static task_t* ready_queue = NULL;
static task_t* ready_queue_tail = NULL;
bool scheduler_enabled = false;
static spinlock_t scheduler_lock = SPINLOCK_INIT;

extern void task_set_current(task_t* task);
extern void switch_task(cpu_context_t* old_ctx, cpu_context_t* new_ctx);

void scheduler_init(void) {
    ready_queue = NULL;
    ready_queue_tail = NULL;
    scheduler_enabled = false;
}

void scheduler_add_task(task_t* task) {
    if (!task) return;
    if (!task->kernel_stack) return;
    if (task->state == TASK_RUNNING) return;

    irq_state_t state = spinlock_acquire_irqsave(&scheduler_lock);

    task->next = NULL;
    task->state = TASK_READY;

    if (!ready_queue) {
        ready_queue = task;
        ready_queue_tail = task;
        spinlock_release_irqrestore(&scheduler_lock, state);
        return;
    }

    task_t* cur = ready_queue;
    task_t* prev = NULL;
    while (cur && cur->priority <= task->priority) {
        prev = cur;
        cur = cur->next;
    }

    if (!prev) {
        task->next = ready_queue;
        ready_queue = task;
    } else {
        task->next = cur;
        prev->next = task;
        if (!cur) ready_queue_tail = task;
    }

    spinlock_release_irqrestore(&scheduler_lock, state);
}

void scheduler_remove_task(task_t* task) {
    if (!task || !ready_queue) return;

    irq_state_t state = spinlock_acquire_irqsave(&scheduler_lock);

    task_t* prev = NULL;
    task_t* cur = ready_queue;
    while (cur) {
        if (cur == task) {
            if (prev) prev->next = cur->next;
            else      ready_queue = cur->next;
            if (ready_queue_tail == cur) ready_queue_tail = prev;
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    spinlock_release_irqrestore(&scheduler_lock, state);
}

void schedule(void) {
    if (!scheduler_enabled || !ready_queue) {
        return;
    }

    task_t* prev = get_current_task();
    if (!prev) return;

    irq_state_t state = spinlock_acquire_irqsave(&scheduler_lock);
    task_t* next = ready_queue;
    if (!next) {
        spinlock_release_irqrestore(&scheduler_lock, state);
        return;
    }
    ready_queue = next->next;
    if (!ready_queue) ready_queue_tail = NULL;
    next->next = NULL;
    next->state = TASK_RUNNING;
    spinlock_release_irqrestore(&scheduler_lock, state);

    if (!prev->is_kernel && prev->state != TASK_TERMINATED && strcmp(next->name, "idle") == 0) {
        scheduler_add_task(next);
        return;
    }

    if (next == prev) {
        scheduler_add_task(next);
        return;
    }

    arch_set_kernel_stack((uint64_t)next->kernel_stack + KERNEL_STACK_SIZE);
    task_set_current(next);
    
    extern uint64_t syscall_kernel_rsp;
    syscall_kernel_rsp = next->syscall_kernel_rsp;

    if (strcmp(next->name, "idle") == 0) {
        next->context.kernel_rsp = (uint64_t)next->kernel_stack + KERNEL_STACK_SIZE;
    }
    
    if (next->is_kernel && (next->context.kernel_rsp < (uint64_t)next->kernel_stack ||
        next->context.kernel_rsp > (uint64_t)next->kernel_stack + KERNEL_STACK_SIZE)) {
        serial_write("[SCHED] PANIC: bad kernel_rsp\n");
        serial_write("[SCHED] kernel_rsp: 0x");
        serial_write_hex(next->context.kernel_rsp);
        serial_write(", kernel_stack: 0x");
        serial_write_hex((uint64_t)next->kernel_stack);
        serial_write(", end: 0x");
        serial_write_hex((uint64_t)next->kernel_stack + KERNEL_STACK_SIZE);
        serial_write("\n");
        panic_halt_message("Scheduler invariant: bad kernel_rsp");
    }

    if (!next->is_kernel && next->context.cr3) {
        vmm_sync_kernel_mappings(next->page_dir);
        uint64_t* new_pml4 = (uint64_t*)(next->page_dir->pml4_phys + 0xFFFF800000000000ULL);
        uint64_t rip;
        __asm__ volatile("lea (%%rip), %0" : "=r"(rip));
        if (!(new_pml4[(rip >> 39) & 0x1FF] & 1)) {
            serial_write("[SCHED] PANIC: kernel not in user pml4\n");
            panic_halt_message("Scheduler invariant: kernel mapping missing in user PML4");
        }
        __asm__ volatile("mov %0, %%cr3" : : "r"(next->context.cr3) : "memory");
    }

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        scheduler_add_task(prev);
    }

    switch_task(&prev->context, &next->context);
}

uint64_t schedule_from_irq(uint64_t* irq_rsp) {
    if (!scheduler_enabled || !ready_queue || !irq_rsp) {
        return 0;
    }

    task_t* prev = get_current_task();
    if (!prev) return 0;

    uint64_t cs = irq_rsp[18];
    if ((cs & 0x3) != 0x3 && prev->pid != 0 && prev->state != TASK_TERMINATED && !prev->kernel_preempt_ok) {
        return 0;
    }

    irq_state_t state = spinlock_acquire_irqsave(&scheduler_lock);
    task_t* next = ready_queue;
    if (!next) {
        spinlock_release_irqrestore(&scheduler_lock, state);
        return 0;
    }
    ready_queue = next->next;
    if (!ready_queue) ready_queue_tail = NULL;
    next->next = NULL;
    next->state = TASK_RUNNING;
    spinlock_release_irqrestore(&scheduler_lock, state);

    if (next == prev) {
        scheduler_add_task(next);
        return 0;
    }

    if (!prev->is_kernel && prev->state != TASK_TERMINATED && strcmp(next->name, "idle") == 0) {
        scheduler_add_task(next);
        return 0;
    }

    prev->context.kernel_rsp = (uint64_t)irq_rsp;

    arch_set_kernel_stack((uint64_t)next->kernel_stack + KERNEL_STACK_SIZE);
    task_set_current(next);

    extern uint64_t syscall_kernel_rsp;
    syscall_kernel_rsp = next->syscall_kernel_rsp;

    if (!next->is_kernel && next->context.cr3) {
        vmm_sync_kernel_mappings(next->page_dir);
        __asm__ volatile("mov %0, %%cr3" : : "r"(next->context.cr3) : "memory");
    }

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        scheduler_add_task(prev);
    }

    return next->context.kernel_rsp;
}

void scheduler_enable(void) {
    scheduler_enabled = true;
}

void scheduler_disable(void) {
    scheduler_enabled = false;
}
