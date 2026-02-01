#ifndef _KERNEL_UACCESS_H
#define _KERNEL_UACCESS_H

#include "stdint.h"

bool copy_from_user(void* kernel_dst, const void* user_src, size_t size);
bool copy_to_user(void* user_dst, const void* kernel_src, size_t size);
ssize_t strnlen_user(const char* user_str, size_t max_len);

#endif