#include <drivers/pic.h>
#include <drivers/serial.h>
#include <kernel/io.h>


void pic_init(void) {
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    
    serial_write("[PIC] Programmable Interrupt Controller initialized\n");
    serial_write("[PIC] All IRQs masked by default\n");
}

void pic_send_eoi(const uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

void pic_set_mask(const uint8_t irq) {
    uint16_t port;
    uint8_t actual_irq = irq;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        actual_irq = irq - 8;
    }
    
    uint8_t value = inb(port) | (1 << actual_irq);
    outb(port, value);
}

void pic_clear_mask(const uint8_t irq) {
    uint16_t port;
    uint8_t actual_irq = irq;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        actual_irq = irq - 8;
    }
    
    uint8_t value = inb(port) & ~(1 << actual_irq);
    outb(port, value);
    
    serial_write("[PIC] Unmasked IRQ ");
    serial_write_dec(irq);
    serial_write("\n");
}