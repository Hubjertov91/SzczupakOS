#ifndef _KERNEL_KEYBOARD_H
#define _KERNEL_KEYBOARD_H

#include "kernel/stdint.h"

void keyboard_init(void);
void keyboard_handler(void);
char keyboard_getchar(void);
bool keyboard_has_input(void);

#endif