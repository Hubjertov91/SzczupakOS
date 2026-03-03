#ifndef _KERNEL_ARCH_API_H
#define _KERNEL_ARCH_API_H

#include <kernel/stdint.h>

void arch_init_early(void);
void arch_init_timer(uint32_t frequency_hz);
void arch_set_kernel_stack(uint64_t stack_top);

void arch_irq_enable(void);
void arch_irq_disable(void);
void arch_wait_for_interrupt(void);
void arch_idle_once(void);
void arch_halt_forever(void) __attribute__((noreturn));

#endif
