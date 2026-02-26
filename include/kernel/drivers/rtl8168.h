#ifndef _KERNEL_DRIVERS_RTL8168_H
#define _KERNEL_DRIVERS_RTL8168_H

#include <kernel/stdint.h>

typedef struct {
    bool present;
    bool initialized;
    bool link_up;
    bool is_pcie;
    uint8_t mac[6];
    uint16_t io_base;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_function;
} rtl8168_info_t;

bool rtl8168_init(void);
bool rtl8168_get_info(rtl8168_info_t* out_info);

#endif
