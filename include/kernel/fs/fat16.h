#ifndef _KERNEL_FAT16_H
#define _KERNEL_FAT16_H

#include "stdint.h"
#include "fs/vfs.h"

vfs_filesystem_t* fat16_create(void);
bool fat16_mount(uint32_t start_lba);

#endif