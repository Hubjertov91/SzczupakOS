#include <drivers/ata.h>

#include <drivers/pci.h>
#include <drivers/serial.h>
#include <kernel/io.h>
#include <kernel/string.h>

#define ATA_CHANNEL_COUNT 2u
#define ATA_DRIVE_PER_CHANNEL 2u
#define ATA_DEVICE_COUNT (ATA_CHANNEL_COUNT * ATA_DRIVE_PER_CHANNEL)

#define ATA_PRIMARY_IO          0x1F0u
#define ATA_PRIMARY_CONTROL     0x3F6u
#define ATA_SECONDARY_IO        0x170u
#define ATA_SECONDARY_CONTROL   0x376u

#define ATA_REG_DATA      0u
#define ATA_REG_ERROR     1u
#define ATA_REG_SECCOUNT  2u
#define ATA_REG_LBA_LO    3u
#define ATA_REG_LBA_MID   4u
#define ATA_REG_LBA_HI    5u
#define ATA_REG_DRIVE     6u
#define ATA_REG_STATUS    7u
#define ATA_REG_COMMAND   7u

#define ATA_CMD_READ_PIO        0x20u
#define ATA_CMD_READ_PIO_EXT    0x24u
#define ATA_CMD_WRITE_PIO       0x30u
#define ATA_CMD_WRITE_PIO_EXT   0x34u
#define ATA_CMD_CACHE_FLUSH     0xE7u
#define ATA_CMD_CACHE_FLUSH_EXT 0xEAu
#define ATA_CMD_IDENTIFY        0xECu

#define ATA_STATUS_ERR 0x01u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_DF  0x20u
#define ATA_STATUS_BSY 0x80u

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
} ata_channel_t;

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
} ata_device_t;

typedef struct {
    bool initialized;
    bool controller_found;
    ata_channel_t channels[ATA_CHANNEL_COUNT];
    ata_device_t devices[ATA_DEVICE_COUNT];
    int8_t selected_index;
} ata_state_t;

static ata_state_t g_ata;

static uint8_t ata_device_index(uint8_t channel, uint8_t drive) {
    return (uint8_t)(channel * ATA_DRIVE_PER_CHANNEL + drive);
}

static void ata_io_delay(uint16_t ctrl_base) {
    if (ctrl_base != 0u) {
        (void)inb(ctrl_base);
        (void)inb(ctrl_base);
        (void)inb(ctrl_base);
        (void)inb(ctrl_base);
    } else {
        io_wait();
    }
}

static bool ata_wait_not_bsy(uint16_t io_base, uint32_t timeout) {
    while (timeout-- > 0u) {
        uint8_t status = inb((uint16_t)(io_base + ATA_REG_STATUS));
        if (status == 0u) {
            return false;
        }
        if ((status & ATA_STATUS_BSY) == 0u) {
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool ata_wait_drq(uint16_t io_base, uint32_t timeout) {
    while (timeout-- > 0u) {
        uint8_t status = inb((uint16_t)(io_base + ATA_REG_STATUS));
        if (status == 0u) {
            return false;
        }
        if ((status & ATA_STATUS_BSY) != 0u) {
            __asm__ volatile("pause");
            continue;
        }
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0u) {
            return false;
        }
        if ((status & ATA_STATUS_DRQ) != 0u) {
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static void ata_select_drive(const ata_channel_t* channel, uint8_t drive) {
    outb((uint16_t)(channel->io_base + ATA_REG_DRIVE), (uint8_t)(0xA0u | ((drive & 1u) << 4)));
    ata_io_delay(channel->ctrl_base);
}

static void ata_identify_model(const uint16_t identify[256], char out_model[41]) {
    if (!identify || !out_model) {
        return;
    }

    for (uint8_t i = 0u; i < 20u; i++) {
        uint16_t word = identify[27u + i];
        out_model[i * 2u] = (char)(word >> 8);
        out_model[i * 2u + 1u] = (char)(word & 0xFFu);
    }
    out_model[40] = '\0';

    for (int32_t i = 39; i >= 0; i--) {
        if (out_model[i] == ' ' || out_model[i] == '\0') {
            out_model[i] = '\0';
        } else {
            break;
        }
    }

    size_t start = 0;
    while (out_model[start] == ' ') {
        start++;
    }
    if (start > 0) {
        memmove(out_model, out_model + start, 41u - start);
    }
    if (out_model[0] == '\0') {
        strcpy(out_model, "ATA device");
    }
}

static bool ata_identify_device(uint8_t channel_idx, uint8_t drive_idx, ata_device_t* out_device) {
    if (!out_device || channel_idx >= ATA_CHANNEL_COUNT || drive_idx >= ATA_DRIVE_PER_CHANNEL) {
        return false;
    }

    ata_channel_t* channel = &g_ata.channels[channel_idx];
    memset(out_device, 0, sizeof(*out_device));
    out_device->channel = channel_idx;
    out_device->drive = drive_idx;
    out_device->io_base = channel->io_base;
    out_device->ctrl_base = channel->ctrl_base;

    ata_select_drive(channel, drive_idx);

    outb((uint16_t)(channel->io_base + ATA_REG_SECCOUNT), 0u);
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_LO), 0u);
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_MID), 0u);
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_HI), 0u);
    outb((uint16_t)(channel->io_base + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    ata_io_delay(channel->ctrl_base);

    uint8_t status = inb((uint16_t)(channel->io_base + ATA_REG_STATUS));
    if (status == 0u) {
        return false;
    }

    if (!ata_wait_not_bsy(channel->io_base, 200000u)) {
        return false;
    }

    uint8_t lba_mid = inb((uint16_t)(channel->io_base + ATA_REG_LBA_MID));
    uint8_t lba_hi = inb((uint16_t)(channel->io_base + ATA_REG_LBA_HI));
    if (lba_mid != 0u || lba_hi != 0u) {
        out_device->present = true;
        out_device->atapi = true;
        strcpy(out_device->model, "ATAPI/unsupported");
        return true;
    }

    if (!ata_wait_drq(channel->io_base, 200000u)) {
        return false;
    }

    uint16_t identify[256];
    for (uint16_t i = 0; i < 256u; i++) {
        identify[i] = inw((uint16_t)(channel->io_base + ATA_REG_DATA));
    }

    out_device->present = true;
    out_device->atapi = false;
    out_device->lba48 = (identify[83] & (1u << 10)) != 0u;

    uint64_t sectors = (uint64_t)(((uint32_t)identify[61] << 16) | identify[60]);
    if (out_device->lba48) {
        uint64_t ext = (uint64_t)identify[100] |
                       ((uint64_t)identify[101] << 16) |
                       ((uint64_t)identify[102] << 32) |
                       ((uint64_t)identify[103] << 48);
        if (ext != 0u) {
            sectors = ext;
        }
    }
    out_device->sector_count = sectors;

    ata_identify_model(identify, out_device->model);
    return true;
}

static uint16_t ata_io_bar_to_base(uint32_t bar) {
    if ((bar & 0x1u) == 0u) {
        return 0u;
    }
    uint16_t base = (uint16_t)(bar & 0xFFFCu);
    if (base == 0u || base == 0xFFFFu) {
        return 0u;
    }
    return base;
}

static void ata_apply_legacy_defaults(void) {
    g_ata.channels[0].io_base = ATA_PRIMARY_IO;
    g_ata.channels[0].ctrl_base = ATA_PRIMARY_CONTROL;
    g_ata.channels[1].io_base = ATA_SECONDARY_IO;
    g_ata.channels[1].ctrl_base = ATA_SECONDARY_CONTROL;
}

static void ata_detect_controller_layout(void) {
    ata_apply_legacy_defaults();

    uint16_t count = pci_get_device_count();
    for (uint16_t i = 0; i < count; i++) {
        pci_device_t dev;
        if (!pci_get_device(i, &dev)) {
            continue;
        }
        if (dev.class_code != 0x01u || dev.subclass != 0x01u) {
            continue;
        }

        g_ata.controller_found = true;

        (void)pci_set_command_bits(dev.bus, dev.slot, dev.function,
                                   (uint16_t)(PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER));

        uint8_t prog_if = dev.prog_if;
        if ((prog_if & 0x01u) != 0u) {
            uint16_t pio = ata_io_bar_to_base(pci_read_bar(dev.bus, dev.slot, dev.function, 0u));
            uint16_t pctrl = ata_io_bar_to_base(pci_read_bar(dev.bus, dev.slot, dev.function, 1u));
            if (pio != 0u) {
                g_ata.channels[0].io_base = pio;
            }
            if (pctrl != 0u) {
                g_ata.channels[0].ctrl_base = (uint16_t)(pctrl + 2u);
            }
        }

        if ((prog_if & 0x04u) != 0u) {
            uint16_t sio = ata_io_bar_to_base(pci_read_bar(dev.bus, dev.slot, dev.function, 2u));
            uint16_t sctrl = ata_io_bar_to_base(pci_read_bar(dev.bus, dev.slot, dev.function, 3u));
            if (sio != 0u) {
                g_ata.channels[1].io_base = sio;
            }
            if (sctrl != 0u) {
                g_ata.channels[1].ctrl_base = (uint16_t)(sctrl + 2u);
            }
        }

        serial_write("[ATA] IDE controller ");
        serial_write("at ");
        serial_write_dec(dev.bus);
        serial_write_char(':');
        serial_write_dec(dev.slot);
        serial_write_char('.');
        serial_write_dec(dev.function);
        serial_write(", prog_if=");
        serial_write_hex((uint64_t)prog_if);
        serial_write("\n");
        break;
    }
}

static void ata_log_device(const ata_device_t* dev) {
    if (!dev || !dev->present) {
        return;
    }

    serial_write("[ATA] ");
    serial_write((dev->channel == 0u) ? "primary/" : "secondary/");
    serial_write((dev->drive == 0u) ? "master: " : "slave: ");

    if (dev->atapi) {
        serial_write("ATAPI/unsupported\n");
        return;
    }

    serial_write(dev->model);
    serial_write(", sectors=");
    if (dev->sector_count > 0xFFFFFFFFu) {
        serial_write_dec((uint32_t)(dev->sector_count >> 32));
        serial_write("*2^32+");
        serial_write_dec((uint32_t)(dev->sector_count & 0xFFFFFFFFu));
    } else {
        serial_write_dec((uint32_t)dev->sector_count);
    }
    serial_write(", ");
    serial_write(dev->lba48 ? "LBA48" : "LBA28");
    serial_write("\n");
}

static void ata_probe_devices(void) {
    memset(g_ata.devices, 0, sizeof(g_ata.devices));
    g_ata.selected_index = -1;

    for (uint8_t channel = 0u; channel < ATA_CHANNEL_COUNT; channel++) {
        for (uint8_t drive = 0u; drive < ATA_DRIVE_PER_CHANNEL; drive++) {
            uint8_t index = ata_device_index(channel, drive);
            ata_device_t dev;
            if (!ata_identify_device(channel, drive, &dev)) {
                continue;
            }
            g_ata.devices[index] = dev;
            ata_log_device(&dev);

            if (g_ata.selected_index < 0 && dev.present && !dev.atapi) {
                g_ata.selected_index = (int8_t)index;
            }
        }
    }
}

static void ata_ensure_initialized(void) {
    if (!g_ata.initialized) {
        ata_init();
    }
}

void ata_init(void) {
    if (g_ata.initialized) {
        return;
    }

    memset(&g_ata, 0, sizeof(g_ata));
    serial_write("[ATA] Initializing ATA/IDE driver\n");

    ata_detect_controller_layout();
    ata_probe_devices();

    g_ata.initialized = true;

    if (g_ata.selected_index < 0) {
        serial_write("[ATA] No ATA disk available for PIO\n");
        return;
    }

    ata_device_t* selected = &g_ata.devices[(uint8_t)g_ata.selected_index];
    serial_write("[ATA] Selected ");
    serial_write((selected->channel == 0u) ? "primary/" : "secondary/");
    serial_write((selected->drive == 0u) ? "master" : "slave");
    serial_write(", IO=");
    serial_write_hex((uint64_t)selected->io_base);
    serial_write(", CTRL=");
    serial_write_hex((uint64_t)selected->ctrl_base);
    serial_write("\n");
}

bool ata_is_ready(void) {
    ata_ensure_initialized();
    return g_ata.selected_index >= 0;
}

bool ata_get_selected_drive(ata_drive_info_t* out_info) {
    ata_ensure_initialized();
    if (!out_info || g_ata.selected_index < 0) {
        return false;
    }

    ata_device_t* dev = &g_ata.devices[(uint8_t)g_ata.selected_index];
    memset(out_info, 0, sizeof(*out_info));
    out_info->present = dev->present;
    out_info->atapi = dev->atapi;
    out_info->lba48 = dev->lba48;
    out_info->channel = dev->channel;
    out_info->drive = dev->drive;
    out_info->io_base = dev->io_base;
    out_info->ctrl_base = dev->ctrl_base;
    out_info->sector_count = dev->sector_count;
    memcpy(out_info->model, dev->model, sizeof(out_info->model));
    return true;
}

static bool ata_setup_command_lba28(const ata_device_t* dev, uint32_t lba, uint8_t command) {
    if (!dev || lba > 0x0FFFFFFFu) {
        return false;
    }

    ata_channel_t* channel = &g_ata.channels[dev->channel];
    outb((uint16_t)(channel->io_base + ATA_REG_DRIVE),
         (uint8_t)(0xE0u | ((dev->drive & 1u) << 4) | ((lba >> 24) & 0x0Fu)));
    ata_io_delay(channel->ctrl_base);

    outb((uint16_t)(channel->io_base + ATA_REG_SECCOUNT), 1u);
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_LO), (uint8_t)(lba & 0xFFu));
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 8) & 0xFFu));
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_HI), (uint8_t)((lba >> 16) & 0xFFu));
    outb((uint16_t)(channel->io_base + ATA_REG_COMMAND), command);
    return true;
}

static bool ata_setup_command_lba48(const ata_device_t* dev, uint32_t lba, uint8_t command) {
    if (!dev || !dev->lba48) {
        return false;
    }

    ata_channel_t* channel = &g_ata.channels[dev->channel];
    outb((uint16_t)(channel->io_base + ATA_REG_DRIVE), (uint8_t)(0x40u | ((dev->drive & 1u) << 4)));
    ata_io_delay(channel->ctrl_base);

    outb((uint16_t)(channel->io_base + ATA_REG_SECCOUNT), 0u);
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_LO), (uint8_t)((lba >> 24) & 0xFFu));
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_MID), 0u);
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_HI), 0u);

    outb((uint16_t)(channel->io_base + ATA_REG_SECCOUNT), 1u);
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_LO), (uint8_t)(lba & 0xFFu));
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 8) & 0xFFu));
    outb((uint16_t)(channel->io_base + ATA_REG_LBA_HI), (uint8_t)((lba >> 16) & 0xFFu));
    outb((uint16_t)(channel->io_base + ATA_REG_COMMAND), command);
    return true;
}

bool ata_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!buffer) {
        return false;
    }

    ata_ensure_initialized();
    if (g_ata.selected_index < 0) {
        return false;
    }

    ata_device_t* dev = &g_ata.devices[(uint8_t)g_ata.selected_index];
    if (!dev->present || dev->atapi) {
        return false;
    }
    if (dev->sector_count != 0u && (uint64_t)lba >= dev->sector_count) {
        return false;
    }

    ata_channel_t* channel = &g_ata.channels[dev->channel];
    ata_select_drive(channel, dev->drive);
    if (!ata_wait_not_bsy(channel->io_base, 200000u)) {
        return false;
    }

    bool ok;
    if (dev->lba48 && lba > 0x0FFFFFFFu) {
        ok = ata_setup_command_lba48(dev, lba, ATA_CMD_READ_PIO_EXT);
    } else {
        ok = ata_setup_command_lba28(dev, lba, ATA_CMD_READ_PIO);
    }
    if (!ok) {
        return false;
    }

    if (!ata_wait_drq(channel->io_base, 200000u)) {
        return false;
    }

    uint16_t* out_words = (uint16_t*)buffer;
    for (uint16_t i = 0; i < 256u; i++) {
        out_words[i] = inw((uint16_t)(channel->io_base + ATA_REG_DATA));
    }
    return true;
}

bool ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    if (!buffer) {
        return false;
    }

    ata_ensure_initialized();
    if (g_ata.selected_index < 0) {
        return false;
    }

    ata_device_t* dev = &g_ata.devices[(uint8_t)g_ata.selected_index];
    if (!dev->present || dev->atapi) {
        return false;
    }
    if (dev->sector_count != 0u && (uint64_t)lba >= dev->sector_count) {
        return false;
    }

    ata_channel_t* channel = &g_ata.channels[dev->channel];
    ata_select_drive(channel, dev->drive);
    if (!ata_wait_not_bsy(channel->io_base, 200000u)) {
        return false;
    }

    bool use_ext = dev->lba48 && lba > 0x0FFFFFFFu;
    bool ok = use_ext
                ? ata_setup_command_lba48(dev, lba, ATA_CMD_WRITE_PIO_EXT)
                : ata_setup_command_lba28(dev, lba, ATA_CMD_WRITE_PIO);
    if (!ok) {
        return false;
    }

    if (!ata_wait_drq(channel->io_base, 200000u)) {
        return false;
    }

    const uint16_t* in_words = (const uint16_t*)buffer;
    for (uint16_t i = 0; i < 256u; i++) {
        outw((uint16_t)(channel->io_base + ATA_REG_DATA), in_words[i]);
    }

    outb((uint16_t)(channel->io_base + ATA_REG_COMMAND),
         use_ext ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
    return ata_wait_not_bsy(channel->io_base, 200000u);
}
