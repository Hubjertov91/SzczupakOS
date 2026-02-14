#ifndef _KERNEL_ATA_H
#define _KERNEL_ATA_H

#include "stdint.h"

void ata_init(void);
bool ata_read_sector(uint32_t lba, uint8_t* buffer);
bool ata_write_sector(uint32_t lba, const uint8_t* buffer);

#endif