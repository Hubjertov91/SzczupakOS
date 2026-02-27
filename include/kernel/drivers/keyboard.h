#ifndef _KERNEL_KEYBOARD_H
#define _KERNEL_KEYBOARD_H

#include "stdint.h"

void keyboard_init(void);
void keyboard_handler(void);
void keyboard_inject_char(char c);
char keyboard_getchar(void);
bool keyboard_has_input(void);
void keyboard_set_usb_hid_active(bool active);
uint32_t keyboard_get_drop_count(void);

#endif
