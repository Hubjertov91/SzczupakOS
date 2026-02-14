#pragma once

#include "stdint.h"

void heap_init(void);

void* kmalloc(size_t size);
void* kcalloc(size_t n, size_t size);
void* krealloc(void* ptr, size_t size);
void  kfree(void* ptr);

void heap_print_stats(void);
