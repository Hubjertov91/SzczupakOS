#ifndef _KERNEL_UACCESS_H
#define _KERNEL_UACCESS_H


#include "stdint.h"

int copy_from_user(void* kernel_dst, const void* user_src, size_t size);
int copy_to_user(void* user_dst, const void* kernel_src, size_t size);
ssize_t strnlen_user(const char* user_str, size_t max_len);
int strncpy_from_user(char* kernel_dst, const char* user_src, size_t max_len);
int verify_user_read(const void* user_ptr, size_t size);
int verify_user_write(void* user_ptr, size_t size);
int clear_user(void* user_dst, size_t size);

int get_user_u8(uint8_t* kernel_dst, const uint8_t* user_src);
int get_user_u16(uint16_t* kernel_dst, const uint16_t* user_src);
int get_user_u32(uint32_t* kernel_dst, const uint32_t* user_src);
int get_user_u64(uint64_t* kernel_dst, const uint64_t* user_src);
int put_user_u8(uint8_t* user_dst, uint8_t value);
int put_user_u16(uint16_t* user_dst, uint16_t value);
int put_user_u32(uint32_t* user_dst, uint32_t value);
int put_user_u64(uint64_t* user_dst, uint64_t value);

void uaccess_init(void);

#endif