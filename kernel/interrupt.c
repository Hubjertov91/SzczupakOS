
#include <kernel/serial.h>
#include <kernel/idt.h>
#include <kernel/pagefault.h>

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
    
    idt_set_gate(0, (uint64_t)isr0, 0x8E);
    idt_set_gate(1, (uint64_t)isr1, 0x8E);
    idt_set_gate(2, (uint64_t)isr2, 0x8E);
    idt_set_gate(3, (uint64_t)isr3, 0x8E);
    idt_set_gate(4, (uint64_t)isr4, 0x8E);
    idt_set_gate(5, (uint64_t)isr5, 0x8E);
    idt_set_gate(6, (uint64_t)isr6, 0x8E);
    idt_set_gate(7, (uint64_t)isr7, 0x8E);
    idt_set_gate(8, (uint64_t)isr8, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x8E);

    idt_set_gate(32, (uint64_t)irq0, 0x8E);
    idt_set_gate(33, (uint64_t)irq1, 0x8E);
    idt_set_gate(34, (uint64_t)irq2, 0x8E);
    idt_set_gate(35, (uint64_t)irq3, 0x8E);
    idt_set_gate(36, (uint64_t)irq4, 0x8E);
    idt_set_gate(37, (uint64_t)irq5, 0x8E);
    idt_set_gate(38, (uint64_t)irq6, 0x8E);
    idt_set_gate(39, (uint64_t)irq7, 0x8E);
    idt_set_gate(40, (uint64_t)irq8, 0x8E);
    idt_set_gate(41, (uint64_t)irq9, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x8E);
    
    __asm__ volatile("lidt %0" : : "m"(idtp));
    
    serial_write("[IDT] Interrupt Descriptor Table initialized\n");
}

void exception_handler(uint64_t vector, uint64_t error_code, uint64_t rip) {

    if (vector == 14) {
        uint64_t faulting_addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_addr));
        pagefault_handler(error_code, faulting_addr);
        return;
    }

    const char* exceptions[] = {
        "Division By Zero",
        "Debug",
        "NMI",
        "Breakpoint",
        "Overflow",
        "Bound Range",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection",
        "Page Fault"
    };
    
    serial_write("\n========================================\n");
    serial_write("[EXCEPTION] ");
    
    if (vector < 15) {
        serial_write(exceptions[vector]);
    } else {
        serial_write("Unknown Exception");
    }
    
    serial_write("\n========================================\n");
    serial_write("Vector:     ");
    serial_write_hex(vector);
    serial_write("\nError Code: ");
    serial_write_hex(error_code);
    serial_write("\nRIP:        ");
    serial_write_hex(rip);
    serial_write("\n========================================\n");
    serial_write("System halted.\n");
    
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    const char* msg = "!!! KERNEL PANIC - Check serial output !!!";
    for (int i = 0; msg[i] != '\0'; i++) {
        vga[160 + i] = 0x4F00 | msg[i];  
    }
    
    __asm__ volatile("cli");
    while(1) {
        __asm__ volatile("hlt");
    }
    
    __builtin_unreachable();
}