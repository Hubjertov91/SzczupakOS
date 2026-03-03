#include <drivers/serial.h>
#include <arch/idt.h>
#include <mm/pagefault.h>
#include <debug/panic.h>

#define IDT_TRAP_GATE   0x8F
#define IDT_INTR_GATE   0x8E

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

void idt_set_gate(uint8_t num, uint64_t handler, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = 0x08;
    idt[num].ist = 0;
    idt[num].type_attr = flags;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].reserved = 0;
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)&idt;
    
    idt_set_gate(0, (uint64_t)isr0, IDT_TRAP_GATE);
    idt_set_gate(1, (uint64_t)isr1, IDT_TRAP_GATE);
    idt_set_gate(2, (uint64_t)isr2, IDT_TRAP_GATE);
    idt_set_gate(3, (uint64_t)isr3, IDT_TRAP_GATE);
    idt_set_gate(4, (uint64_t)isr4, IDT_TRAP_GATE);
    idt_set_gate(5, (uint64_t)isr5, IDT_TRAP_GATE);
    idt_set_gate(6, (uint64_t)isr6, IDT_TRAP_GATE);
    idt_set_gate(7, (uint64_t)isr7, IDT_TRAP_GATE);
    idt_set_gate(8, (uint64_t)isr8, IDT_TRAP_GATE);
    idt_set_gate(9, (uint64_t)isr9, IDT_TRAP_GATE);
    idt_set_gate(10, (uint64_t)isr10, IDT_TRAP_GATE);
    idt_set_gate(11, (uint64_t)isr11, IDT_TRAP_GATE);
    idt_set_gate(12, (uint64_t)isr12, IDT_TRAP_GATE);
    idt_set_gate(13, (uint64_t)isr13, IDT_TRAP_GATE);
    idt_set_gate(14, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(16, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(17, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(18, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(19, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(20, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(21, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(22, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(23, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(24, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(25, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(26, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(27, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(28, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(29, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(30, (uint64_t)isr14, IDT_TRAP_GATE);
    idt_set_gate(31, (uint64_t)isr14, IDT_TRAP_GATE);

    idt_set_gate(32, (uint64_t)irq0, IDT_INTR_GATE);
    idt_set_gate(33, (uint64_t)irq1, IDT_INTR_GATE);
    idt_set_gate(34, (uint64_t)irq2, IDT_INTR_GATE);
    idt_set_gate(35, (uint64_t)irq3, IDT_INTR_GATE);
    idt_set_gate(36, (uint64_t)irq4, IDT_INTR_GATE);
    idt_set_gate(37, (uint64_t)irq5, IDT_INTR_GATE);
    idt_set_gate(38, (uint64_t)irq6, IDT_INTR_GATE);
    idt_set_gate(39, (uint64_t)irq7, IDT_INTR_GATE);
    idt_set_gate(40, (uint64_t)irq8, IDT_INTR_GATE);
    idt_set_gate(41, (uint64_t)irq9, IDT_INTR_GATE);
    idt_set_gate(42, (uint64_t)irq10, IDT_INTR_GATE);
    idt_set_gate(43, (uint64_t)irq11, IDT_INTR_GATE);
    idt_set_gate(44, (uint64_t)irq12, IDT_INTR_GATE);
    idt_set_gate(45, (uint64_t)irq13, IDT_INTR_GATE);
    idt_set_gate(46, (uint64_t)irq14, IDT_INTR_GATE);
    idt_set_gate(47, (uint64_t)irq15, IDT_INTR_GATE);
    
    __asm__ volatile("lidt %0" : : "m"(idtp));
    
    serial_write("[IDT] Interrupt Descriptor Table initialized\n");
}

static uint64_t trap_rbp(uint64_t frame_ptr) {
    if (!frame_ptr) return 0u;
    return ((uint64_t*)frame_ptr)[8];
}

static uint64_t trap_rsp(uint64_t cs, uint64_t frame_ptr) {
    if (!frame_ptr) return 0u;
    if ((cs & 0x3u) == 0x3u) {
        return ((uint64_t*)frame_ptr)[20];
    }
    return frame_ptr + 160u;
}

void exception_handler(uint64_t vector,
                       uint64_t error_code,
                       uint64_t rip,
                       uint64_t cs,
                       uint64_t rflags,
                       uint64_t frame_ptr) {
    if (vector == 14) {
        uint64_t faulting_addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_addr));
        pagefault_handler(error_code, faulting_addr, rip, cs, rflags, frame_ptr);
        return;
    }

    panic_context_t ctx;
    ctx.vector = vector;
    ctx.error_code = error_code;
    ctx.rip = rip;
    ctx.cs = cs;
    ctx.rflags = rflags;
    ctx.rsp = trap_rsp(cs, frame_ptr);
    ctx.rbp = trap_rbp(frame_ptr);
    ctx.cr2 = 0u;

    panic_dump_and_halt(panic_exception_name(vector), &ctx);
}
