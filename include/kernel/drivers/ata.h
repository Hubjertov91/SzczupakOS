#ifndef _KERNEL_ATA_H
#define _KERNEL_ATA_H

#include "stdint.h"

typedef struct {
    bool present;
    bool atapi;
    bool lba48;
    uint8_t channel;
    uint8_t drive;
    uint16_t io_base;
    uint16_t ctrl_base;
    uint64_t sector_count;
    char model[41];
} ata_drive_info_t;

void ata_init(void);
bool ata_is_ready(void);
bool ata_get_selected_drive(ata_drive_info_t* out_info);
bool ata_read_sector(uint32_t lba, uint8_t* buffer);
bool ata_write_sector(uint32_t lba, const uint8_t* buffer);

#endif
