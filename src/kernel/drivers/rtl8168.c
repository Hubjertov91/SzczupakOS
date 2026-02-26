#include <drivers/rtl8168.h>

#include <drivers/pci.h>
#include <drivers/serial.h>
#include <kernel/io.h>
#include <kernel/string.h>

#define RTL8168_VENDOR_ID 0x10ECu
#define RTL8168_DEVICE_ID 0x8168u
#define RTL8161_DEVICE_ID 0x8161u
#define RTL8169_DEVICE_ID 0x8169u

#define RTL_REG_IDR0    0x00u
#define RTL_REG_CR      0x37u
#define RTL_REG_CONFIG1 0x52u
#define RTL_REG_PHYSTAT 0x6Cu

#define RTL_CR_RESET          0x10u
#define RTL_PHYSTAT_LINK_UP   0x02u

typedef struct {
    bool present;
    bool initialized;
    bool link_up;
    bool is_pcie;
    uint8_t mac[6];
    uint16_t io_base;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
} rtl8168_state_t;

static rtl8168_state_t g_rtl8168;

static bool rtl8168_find_supported(pci_device_t* out_device) {
    if (!out_device) {
        return false;
    }

    static const uint16_t supported_ids[] = {
        RTL8168_DEVICE_ID,
        RTL8161_DEVICE_ID,
        RTL8169_DEVICE_ID
    };

    for (uint8_t i = 0; i < (uint8_t)(sizeof(supported_ids) / sizeof(supported_ids[0])); i++) {
        if (pci_find_device(RTL8168_VENDOR_ID, supported_ids[i], out_device)) {
            return true;
        }
    }

    return false;
}

static uint16_t rtl8168_pick_io_base(const pci_device_t* device) {
    if (!device) {
        return 0u;
    }

    uint32_t bar0 = pci_read_bar(device->bus, device->slot, device->function, 0u);
    if ((bar0 & 0x1u) != 0u) {
        return (uint16_t)(bar0 & 0xFFFCu);
    }

    uint32_t bar1 = pci_read_bar(device->bus, device->slot, device->function, 1u);
    if ((bar1 & 0x1u) != 0u) {
        return (uint16_t)(bar1 & 0xFFFCu);
    }

    return 0u;
}

static void serial_write_hex_byte(uint8_t value) {
    const char* hex = "0123456789ABCDEF";
    serial_write_char(hex[value >> 4]);
    serial_write_char(hex[value & 0x0Fu]);
}

static void serial_write_mac(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < 6u; i++) {
        serial_write_hex_byte(mac[i]);
        if (i + 1u < 6u) {
            serial_write_char(':');
        }
    }
}

bool rtl8168_init(void) {
    memset(&g_rtl8168, 0, sizeof(g_rtl8168));

    pci_device_t dev;
    memset(&dev, 0, sizeof(dev));
    if (!rtl8168_find_supported(&dev)) {
        serial_write("[RTL8168] Not present, skipping\n");
        return false;
    }

    g_rtl8168.present = true;
    g_rtl8168.bus = dev.bus;
    g_rtl8168.slot = dev.slot;
    g_rtl8168.function = dev.function;
    g_rtl8168.is_pcie = dev.is_pcie;

    (void)pci_set_command_bits(dev.bus, dev.slot, dev.function,
                               (uint16_t)(PCI_COMMAND_IO_SPACE |
                                          PCI_COMMAND_MEMORY_SPACE |
                                          PCI_COMMAND_BUS_MASTER));

    uint16_t io_base = rtl8168_pick_io_base(&dev);
    if (io_base == 0u) {
        serial_write("[RTL8168] No I/O BAR exposed (MMIO-only not yet supported)\n");
        return false;
    }

    g_rtl8168.io_base = io_base;

    outb((uint16_t)(io_base + RTL_REG_CONFIG1), 0x00u);

    outb((uint16_t)(io_base + RTL_REG_CR), RTL_CR_RESET);
    for (uint32_t i = 0; i < 200000u; i++) {
        if ((inb((uint16_t)(io_base + RTL_REG_CR)) & RTL_CR_RESET) == 0u) {
            break;
        }
        __asm__ volatile("pause");
    }

    if ((inb((uint16_t)(io_base + RTL_REG_CR)) & RTL_CR_RESET) != 0u) {
        serial_write("[RTL8168] Reset timeout\n");
        return false;
    }

    for (uint8_t i = 0; i < 6u; i++) {
        g_rtl8168.mac[i] = inb((uint16_t)(io_base + RTL_REG_IDR0 + i));
    }

    uint8_t phy_status = inb((uint16_t)(io_base + RTL_REG_PHYSTAT));
    g_rtl8168.link_up = (phy_status & RTL_PHYSTAT_LINK_UP) != 0u;
    g_rtl8168.initialized = true;

    serial_write("[RTL8168] Detected ");
    serial_write_hex((uint64_t)dev.device_id);
    serial_write(" at ");
    serial_write_dec(dev.bus);
    serial_write_char(':');
    serial_write_dec(dev.slot);
    serial_write_char('.');
    serial_write_dec(dev.function);
    serial_write(", IO=");
    serial_write_hex((uint64_t)io_base);
    serial_write(", ");
    serial_write(dev.is_pcie ? "PCIe" : "PCI");
    serial_write(", MAC ");
    serial_write_mac(g_rtl8168.mac);
    serial_write(", link ");
    serial_write(g_rtl8168.link_up ? "up" : "down");
    serial_write("\n");

    return true;
}

bool rtl8168_get_info(rtl8168_info_t* out_info) {
    if (!out_info) {
        return false;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->present = g_rtl8168.present;
    out_info->initialized = g_rtl8168.initialized;
    out_info->link_up = g_rtl8168.link_up;
    out_info->is_pcie = g_rtl8168.is_pcie;
    out_info->io_base = g_rtl8168.io_base;
    out_info->pci_bus = g_rtl8168.bus;
    out_info->pci_slot = g_rtl8168.slot;
    out_info->pci_function = g_rtl8168.function;
    memcpy(out_info->mac, g_rtl8168.mac, sizeof(out_info->mac));

    return true;
}
