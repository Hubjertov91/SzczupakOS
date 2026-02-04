#include <drivers/ata.h>
#include <kernel/serial.h>
#include <kernel/io.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CONTROL 0x376

#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LO     3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HI     5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

static uint16_t ata_base = ATA_PRIMARY_IO;

static void ata_wait_bsy(void) {
    while (inb(ata_base + ATA_REG_STATUS) & ATA_STATUS_BSY);
}

static void ata_wait_drq(void) {
    while (!(inb(ata_base + ATA_REG_STATUS) & ATA_STATUS_DRQ));
}

void ata_init(void) {
    serial_write("[ATA] Initializing ATA/IDE driver\n");
    
    outb(ata_base + ATA_REG_DRIVE, 0xA0);
    outb(ata_base + ATA_REG_SECCOUNT, 0);
    outb(ata_base + ATA_REG_LBA_LO, 0);
    outb(ata_base + ATA_REG_LBA_MID, 0);
    outb(ata_base + ATA_REG_LBA_HI, 0);
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    uint8_t status = inb(ata_base + ATA_REG_STATUS);
    if (status == 0) {
        serial_write("[ATA] No drive detected\n");
        return;
    }
    
    ata_wait_bsy();
    
    uint8_t lba_mid = inb(ata_base + ATA_REG_LBA_MID);
    uint8_t lba_hi = inb(ata_base + ATA_REG_LBA_HI);
    
    if (lba_mid != 0 || lba_hi != 0) {
        serial_write("[ATA] Not an ATA device\n");
        return;
    }
    
    ata_wait_drq();
    
    for (int i = 0; i < 256; i++) {
        inw(ata_base + ATA_REG_DATA);
    }
    
    serial_write("[ATA] ATA drive detected and initialized\n");
}

bool ata_read_sector(uint32_t lba, uint8_t* buffer) {
    ata_wait_bsy();
    
    outb(ata_base + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ata_base + ATA_REG_SECCOUNT, 1);
    outb(ata_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ata_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    ata_wait_drq();
    
    uint16_t* buf = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        buf[i] = inw(ata_base + ATA_REG_DATA);
    }
    
    return true;
}

bool ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    ata_wait_bsy();
    
    outb(ata_base + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ata_base + ATA_REG_SECCOUNT, 1);
    outb(ata_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ata_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    ata_wait_drq();
    
    const uint16_t* buf = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        outw(ata_base + ATA_REG_DATA, buf[i]);
    }
    
    outb(ata_base + ATA_REG_COMMAND, 0xE7);
    ata_wait_bsy();
    
    return true;
}