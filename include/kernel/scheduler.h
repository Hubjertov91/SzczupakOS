#ifndef _KERNEL_SCHEDULER_H
#define _KERNEL_SCHEDULER_H

#include "task.h"

void scheduler_init(void);
void schedule(void);
void scheduler_enable(void);
void scheduler_disable(void);
void scheduler_add_task(task_t* task);
void scheduler_remove_task(task_t* task);

#endif