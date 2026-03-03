#ifndef _KERNEL_SERIAL_H
#define _KERNEL_SERIAL_H

#include "stdint.h"

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char* str);
void serial_write_hex(uint64_t val);
void serial_write_dec(uint32_t val);
bool serial_has_data(void);
char serial_read_char(void);
size_t serial_log_snapshot(char* out, size_t out_size);
size_t serial_log_dump_tail(size_t max_bytes);
void serial_log_clear(void);

#endif
