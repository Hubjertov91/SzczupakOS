#include <task/tss.h>
#include <drivers/serial.h>
#include <mm/heap.h>

static tss_t tss;
static uint8_t* ist1_stack;

extern uint64_t syscall_kernel_rsp;

void tss_init(void) {
    for (size_t i = 0; i < sizeof(tss_t); i++) {
        ((uint8_t*)&tss)[i] = 0;
    }

    ist1_stack = kmalloc(8192);
    uint64_t ist1_top = (uint64_t)ist1_stack + 8192;

    tss.rsp0 = 0;
    tss.ist1 = ist1_top - 8;
    tss.iomap_base = sizeof(tss_t);

    serial_write("[TSS] init\n");
}

void tss_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
    syscall_kernel_rsp = stack;
}

tss_t* tss_get_address(void) {
    return &tss;
}