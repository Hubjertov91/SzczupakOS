#ifndef _KERNEL_DRIVERS_GPU_H
#define _KERNEL_DRIVERS_GPU_H

#include <kernel/stdint.h>

typedef struct {
    bool present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t mmio_base;
    uint64_t io_base;
} gpu_info_t;

bool gpu_init(void);
bool gpu_available(void);
bool gpu_get_info(gpu_info_t* out_info);
const char* gpu_backend_name(void);

#endif
