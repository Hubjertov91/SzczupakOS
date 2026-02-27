#ifndef _KERNEL_MOUSE_H
#define _KERNEL_MOUSE_H

#include "stdint.h"

typedef struct {
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    uint8_t changed;
    uint16_t _reserved;
    uint32_t seq;
} mouse_event_t;

void mouse_init(void);
void mouse_handler(void);
void mouse_inject_usb(int8_t dx, int8_t dy, uint8_t buttons);
bool mouse_poll_event(mouse_event_t* out);
void mouse_set_usb_hid_active(bool active);
uint32_t mouse_get_overrun_count(void);

#endif
