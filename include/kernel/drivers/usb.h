#ifndef _KERNEL_USB_H
#define _KERNEL_USB_H

#include <kernel/stdint.h>

#define USB_MAX_CONTROLLERS 16u

#define USB_CTRL_UNKNOWN 0u
#define USB_CTRL_UHCI    1u
#define USB_CTRL_OHCI    2u
#define USB_CTRL_EHCI    3u
#define USB_CTRL_XHCI    4u

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t controller_type;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    bool is_pcie;
    bool initialized;
    uint8_t port_count;
    uint8_t connected_ports;
    uint32_t io_base;
    uint64_t mmio_base;
} usb_controller_info_t;

bool usb_init(void);
void usb_poll(void);
uint16_t usb_get_controller_count(void);
bool usb_get_controller(uint16_t index, usb_controller_info_t* out_info);

#endif
