#include <kernel/tss.h>
#include <kernel/serial.h>

static tss_t tss;

void tss_init(void) {
    for (int i = 0; i < sizeof(tss_t); i++) {
        ((uint8_t*)&tss)[i] = 0;
    }
    
    tss.rsp0 = 0;
    tss.iomap_base = sizeof(tss_t);
    
    serial_write("[TSS] Task State Segment initiazzlized\n");
}

void tss_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
}

tss_t* tss_get_address(void) {
    return &tss;
}