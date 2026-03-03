#include <arch/api.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <drivers/pic.h>
#include <drivers/pit.h>

void arch_init_early(void) {
    idt_init();
    gdt_init();
}

void arch_init_timer(uint32_t frequency_hz) {
    pic_init();
    pit_init(frequency_hz);
}

void arch_set_kernel_stack(uint64_t stack_top) {
    gdt_set_kernel_stack(stack_top);
}

void arch_irq_enable(void) {
    __asm__ volatile("sti" : : : "memory");
}

void arch_irq_disable(void) {
    __asm__ volatile("cli" : : : "memory");
}

void arch_wait_for_interrupt(void) {
    __asm__ volatile("hlt");
}

void arch_idle_once(void) {
    __asm__ volatile("sti; hlt; cli" : : : "memory");
}

void arch_halt_forever(void) {
    arch_irq_disable();
    for (;;) {
        arch_wait_for_interrupt();
    }
}
