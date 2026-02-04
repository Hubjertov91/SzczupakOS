#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/vga.h>
#include <drivers/pic.h>
#include <kernel/serial.h>
#include <kernel/tss.h>
#include <kernel/gdt.h>
#include <kernel/spinlock.h>

#define KERNEL_STACK_SIZE 8192

static task_t* ready_queue = NULL;
static task_t* ready_queue_tail = NULL;
static bool scheduler_enabled = false;
static spinlock_t scheduler_lock = SPINLOCK_INIT;

extern void switch_task(cpu_context_t* old_ctx, cpu_context_t* new_ctx);
extern void task_set_current(task_t* task);

void scheduler_init(void) {
    ready_queue = NULL;
    ready_queue_tail = NULL;
    scheduler_enabled = false;
}

void scheduler_add_task(task_t* task) {
    if (!task) {
        serial_write("[SCHEDULER] ERROR: Attempted to add NULL task\n");
        return;
    }
    
    irq_state_t state = spinlock_acquire_irqsave(&scheduler_lock);
    
    task->next = NULL;
    task->state = TASK_READY;
    
    if (!ready_queue) {
        ready_queue = task;
        ready_queue_tail = task;
        spinlock_release_irqrestore(&scheduler_lock, state);
        return;
    }
    
    task_t* current = ready_queue;
    task_t* prev = NULL;
    
    while (current && current->priority <= task->priority) {
        prev = current;
        current = current->next;
    }
    
    if (!prev) {
        task->next = ready_queue;
        ready_queue = task;
    } else {
        task->next = current;
        prev->next = task;
        if (!current) {
            ready_queue_tail = task;
        }
    }
    
    spinlock_release_irqrestore(&scheduler_lock, state);
}

void scheduler_remove_task(task_t* task) {
    if (!task || !ready_queue) return;
    
    irq_state_t state = spinlock_acquire_irqsave(&scheduler_lock);
    
    task_t* prev = NULL;
    task_t* current = ready_queue;
    
    while (current) {
        if (current == task) {
            if (prev) {
                prev->next = current->next;
            } else {
                ready_queue = current->next;
            }
            
            if (ready_queue_tail == current) {
                ready_queue_tail = prev;
            }
            
            serial_write("[SCHEDULER] Task removed\n");
            break;
        }
        prev = current;
        current = current->next;
    }
    
    spinlock_release_irqrestore(&scheduler_lock, state);
}

void schedule(void) {
    if (!scheduler_enabled || !ready_queue) return;
    
    irq_state_t state = spinlock_acquire_irqsave(&scheduler_lock);
    
    task_t* prev = task_get_current();
    task_t* next = NULL;
    
    task_t* candidate = ready_queue;
    while (candidate) {
        if (candidate->state == TASK_READY || candidate->state == TASK_RUNNING) {
            next = candidate;
            break;
        }
        candidate = candidate->next;
    }
    
    if (!next) {
        spinlock_release_irqrestore(&scheduler_lock, state);
        return;
    }
    
    ready_queue = next->next;
    if (ready_queue) {
        ready_queue_tail = ready_queue;
        while (ready_queue_tail->next) {
            ready_queue_tail = ready_queue_tail->next;
        }
    } else {
        ready_queue_tail = NULL;
    }
    
    next->state = TASK_RUNNING;
    task_set_current(next);
    
    serial_write("[SCHED] Switching to task with CR3=0x");
    serial_write_hex(next->context.cr3);
    serial_write(" RIP=0x");
    serial_write_hex(next->context.rip);
    serial_write("\n");
    
    if (next->is_kernel) {
        tss_set_kernel_stack((uint64_t)next->kernel_stack + next->stack_size);
    } else {
        tss_set_kernel_stack((uint64_t)next->kernel_stack + KERNEL_STACK_SIZE);
    }
    
    if (prev && prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        spinlock_release_irqrestore(&scheduler_lock, state);
        scheduler_add_task(prev);
        switch_task(&prev->context, &next->context);
    } else {
        spinlock_release_irqrestore(&scheduler_lock, state);
        switch_task(NULL, &next->context);
    }
}

void scheduler_enable(void) {
    scheduler_enabled = true;
}

void scheduler_disable(void) {
    scheduler_enabled = false;
}