#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/vga.h>
#include <drivers/pic.h>
#include <kernel/serial.h>
#include <kernel/tss.h>
#include <kernel/gdt.h>
#include <kernel/spinlock.h>
#include <kernel/vmm.h>

#define KERNEL_STACK_SIZE 8192

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
    if (!task->kernel_stack) return;  /* nie dodawaj idle taska */

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
    if (!scheduler_enabled || !ready_queue) return;

    task_t* prev = task_get_current();
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

    if (next == prev) {
        scheduler_add_task(next);
        return;
    }

    tss_set_kernel_stack((uint64_t)next->kernel_stack + KERNEL_STACK_SIZE);
    task_set_current(next);

    if (next->context.kernel_rsp < (uint64_t)next->kernel_stack ||
        next->context.kernel_rsp > (uint64_t)next->kernel_stack + KERNEL_STACK_SIZE) {
        serial_write("[SCHED] PANIC: bad kernel_rsp\n");
        while(1) __asm__ volatile("hlt");
    }

    if (!next->is_kernel && next->context.cr3) {
        uint64_t* new_pml4 = (uint64_t*)(next->page_dir->pml4_phys + 0xFFFF800000000000ULL);
        uint64_t rip;
        __asm__ volatile("lea (%%rip), %0" : "=r"(rip));
        if (!(new_pml4[(rip >> 39) & 0x1FF] & 1)) {
            serial_write("[SCHED] PANIC: kernel not in user pml4\n");
            while(1) __asm__ volatile("hlt");
        }
        __asm__ volatile("mov %0, %%cr3" : : "r"(next->context.cr3) : "memory");
    }

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        scheduler_add_task(prev);
    }

    switch_task(&prev->context, &next->context);
}

void scheduler_enable(void) {
    scheduler_enabled = true;
}

void scheduler_disable(void) {
    scheduler_enabled = false;
}