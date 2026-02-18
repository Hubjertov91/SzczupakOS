#ifndef _KERNEL_SCHEDULER_H
#define _KERNEL_SCHEDULER_H

#include "task.h"
#include "stdint.h"

void scheduler_init(void);
void schedule(void);
uint64_t schedule_from_irq(uint64_t* irq_rsp);
void scheduler_enable(void);
void scheduler_disable(void);
void scheduler_add_task(task_t* task);
void scheduler_remove_task(task_t* task);

#endif