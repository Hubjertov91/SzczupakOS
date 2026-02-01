#ifndef _KERNEL_TERMINAL_H
#define _KERNEL_TERMINAL_H

#include "stdint.h"

void terminal_init(void);
void terminal_write(const char* str, size_t len);
void terminal_clear(void);
size_t terminal_read(char* buf, size_t size);

#endif