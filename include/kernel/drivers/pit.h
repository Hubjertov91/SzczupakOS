#ifndef _KERNEL_PIT_H
#define _KERNEL_PIT_H

#include "stdint.h"

void pit_init(uint32_t frequency);
uint64_t pit_get_ticks(void);
uint64_t pit_get_uptime_seconds(void);

#endif