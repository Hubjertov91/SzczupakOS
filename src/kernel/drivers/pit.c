#include <drivers/pit.h>
#include <kernel/vga.h>
#include <drivers/pic.h>
#include <task/scheduler.h>
#include <drivers/serial.h>
#include <kernel/io.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_FREQUENCY 1193182

static volatile uint64_t system_ticks = 0;
static uint32_t pit_frequency = 0;

void pit_init(uint32_t frequency) {
    if (frequency == 0) {
        serial_write("[PIT] ERROR: Invalid frequency\n");
        return;
    }
    
    pit_frequency = frequency;
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    pic_clear_mask(0);
    serial_write("[PIT] Initialized with frequency ");
    serial_write_dec(frequency);
    serial_write(" Hz\n");
}

uint64_t pit_handler(uint64_t* irq_rsp) {
    system_ticks++;
    pic_send_eoi(0);
    return schedule_from_irq(irq_rsp);
}

uint64_t pit_get_ticks(void) {
    return system_ticks;
}

uint64_t pit_get_uptime_seconds(void) {
    if (pit_frequency == 0) return 0;
    return system_ticks / pit_frequency;
}