#ifndef _KERNEL_TERMINAL_H
#define _KERNEL_TERMINAL_H

#include "stdint.h"

void terminal_init(void);
void terminal_write(const char* str, size_t len);
void terminal_clear(void);
size_t terminal_read(char* buf, size_t size);
void terminal_set_serial_preferred(bool preferred);

#endif
