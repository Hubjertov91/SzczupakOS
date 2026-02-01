#include <drivers/pit.h>
#include <kernel/vga.h>
#include <drivers/pic.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/io.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_FREQUENCY 1193182

static volatile uint64_t system_ticks = 0;
static uint32_t pit_frequency = 0;

void pit_init(uint32_t frequency) {
    pit_frequency = frequency;
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    pic_clear_mask(0);
}

void pit_handler(void) {
    serial_write_char('.');
    system_ticks++;
    pic_send_eoi(0);
    schedule();
}

uint64_t pit_get_ticks(void) {
    return system_ticks;
}

uint64_t pit_get_uptime_seconds(void) {
    if (pit_frequency == 0) return 0;
    return system_ticks / pit_frequency;
}