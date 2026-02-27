#include "usb_priv.h"

usb_controller_info_t g_usb_controllers[USB_MAX_CONTROLLERS];
uint16_t g_usb_count = 0;
bool g_usb_scanned = false;

uhci_host_t g_uhci_hosts[USB_MAX_CONTROLLERS];
usb_hid_device_t g_hid_devices[USB_HID_MAX_DEVICES];
uint8_t g_next_usb_address = 1u;

uint8_t usb_mmio_read8(uint64_t base, uint32_t offset) {
    volatile uint8_t* reg = (volatile uint8_t*)(base + (uint64_t)offset);
    return *reg;
}

uint32_t usb_mmio_read32(uint64_t base, uint32_t offset) {
    volatile uint32_t* reg = (volatile uint32_t*)(base + (uint64_t)offset);
    return *reg;
}

void usb_mmio_write32(uint64_t base, uint32_t offset, uint32_t value) {
    volatile uint32_t* reg = (volatile uint32_t*)(base + (uint64_t)offset);
    *reg = value;
}

bool usb_wait_io16_clear(uint16_t port, uint16_t mask, uint32_t poll_limit) {
    for (uint32_t i = 0; i < poll_limit; i++) {
        if ((inw(port) & mask) == 0u) {
            return true;
        }
    }
    return false;
}

bool usb_wait_io16_set(uint16_t port, uint16_t mask, uint32_t poll_limit) {
    for (uint32_t i = 0; i < poll_limit; i++) {
        if ((inw(port) & mask) == mask) {
            return true;
        }
    }
    return false;
}

bool usb_wait_mmio32_clear(uint64_t base, uint32_t offset, uint32_t mask, uint32_t poll_limit) {
    for (uint32_t i = 0; i < poll_limit; i++) {
        if ((usb_mmio_read32(base, offset) & mask) == 0u) {
            return true;
        }
    }
    return false;
}

bool usb_wait_mmio32_set(uint64_t base, uint32_t offset, uint32_t mask, uint32_t poll_limit) {
    for (uint32_t i = 0; i < poll_limit; i++) {
        if ((usb_mmio_read32(base, offset) & mask) == mask) {
            return true;
        }
    }
    return false;
}

void usb_delay_us(uint32_t us) {
    uint64_t loops = (uint64_t)us * 8u;
    if (loops == 0u) {
        loops = 1u;
    }
    for (uint64_t i = 0u; i < loops; i++) {
        io_wait();
    }
}

void usb_delay_ms(uint32_t ms) {
    for (uint32_t i = 0u; i < ms; i++) {
        usb_delay_us(1000u);
    }
}

uint8_t usb_controller_type_from_prog_if(uint8_t prog_if) {
    if (prog_if == 0x00u) return USB_CTRL_UHCI;
    if (prog_if == 0x10u) return USB_CTRL_OHCI;
    if (prog_if == 0x20u) return USB_CTRL_EHCI;
    if (prog_if == 0x30u) return USB_CTRL_XHCI;
    return USB_CTRL_UNKNOWN;
}

static const char* usb_controller_type_name(uint8_t controller_type) {
    switch (controller_type) {
        case USB_CTRL_UHCI: return "UHCI";
        case USB_CTRL_OHCI: return "OHCI";
        case USB_CTRL_EHCI: return "EHCI";
        case USB_CTRL_XHCI: return "xHCI";
        default: return "USB";
    }
}

const char* usb_xhci_speed_name(uint8_t speed_code) {
    switch (speed_code) {
        case 1u: return "Full";
        case 2u: return "Low";
        case 3u: return "High";
        case 4u: return "Super";
        case 5u: return "Super+";
        default: return "Unknown";
    }
}

const char* usb_full_or_low_speed_name(bool low_speed) {
    return low_speed ? "Low" : "Full";
}

void usb_parse_resources(uint8_t bus, uint8_t slot, uint8_t function,
                         uint32_t* out_io_base, uint64_t* out_mmio_base) {
    if (!out_io_base || !out_mmio_base) return;

    *out_io_base = 0u;
    *out_mmio_base = 0u;

    for (uint8_t bar_index = 0u; bar_index < 6u; bar_index++) {
        uint32_t bar = pci_read_bar(bus, slot, function, bar_index);
        if (bar == 0u || bar == 0xFFFFFFFFu) {
            continue;
        }

        if ((bar & 0x1u) != 0u) {
            if (*out_io_base == 0u) {
                *out_io_base = bar & 0xFFFFFFFCu;
            }
            continue;
        }

        uint8_t memory_type = (uint8_t)((bar >> 1) & 0x3u);
        uint64_t mmio_base = (uint64_t)(bar & 0xFFFFFFF0u);

        if (memory_type == 0x2u && bar_index + 1u < 6u) {
            uint32_t bar_high = pci_read_bar(bus, slot, function, (uint8_t)(bar_index + 1u));
            if (bar_high != 0xFFFFFFFFu) {
                mmio_base |= ((uint64_t)bar_high << 32);
            }
            bar_index++;
        }

        if (*out_mmio_base == 0u && mmio_base != 0u) {
            *out_mmio_base = mmio_base;
        }
    }
}

void usb_log_controller(const usb_controller_info_t* info) {
    if (!info) return;

    serial_write("[USB] ");
    serial_write(usb_controller_type_name(info->controller_type));
    serial_write(" controller at ");
    serial_write_dec(info->bus);
    serial_write_char(':');
    serial_write_dec(info->slot);
    serial_write_char('.');
    serial_write_dec(info->function);
    serial_write(" (");
    serial_write_hex((uint64_t)info->vendor_id);
    serial_write(":");
    serial_write_hex((uint64_t)info->device_id);
    serial_write(")");

    if (info->io_base != 0u) {
        serial_write(", io=");
        serial_write_hex((uint64_t)info->io_base);
    }
    if (info->mmio_base != 0u) {
        serial_write(", mmio=");
        serial_write_hex(info->mmio_base);
    }

    if (info->port_count != 0u) {
        serial_write(", ports=");
        serial_write_dec((uint32_t)info->port_count);
        serial_write(", connected=");
        serial_write_dec((uint32_t)info->connected_ports);
    }

    serial_write(", ");
    serial_write(info->initialized ? "probed" : "detected");
    serial_write("\n");
}

void usb_ehci_legacy_handoff(const pci_device_t* dev, uint64_t mmio_virt) {
    if (!dev || mmio_virt == 0u) {
        return;
    }

    uint32_t hccparams = usb_mmio_read32(mmio_virt, EHCI_CAP_HCCPARAMS);
    uint8_t eecp = (uint8_t)((hccparams >> 8) & 0xFFu);
    if (eecp < 0x40u) {
        return;
    }

    uint32_t legsup = pci_read32(dev->bus, dev->slot, dev->function, eecp);
    if ((legsup & EHCI_LEGSUP_BIOS_OWNED) != 0u) {
        pci_write32(dev->bus, dev->slot, dev->function, eecp, legsup | EHCI_LEGSUP_OS_OWNED);
        for (uint32_t i = 0u; i < USB_HANDOFF_POLL_LIMIT; i++) {
            legsup = pci_read32(dev->bus, dev->slot, dev->function, eecp);
            if ((legsup & EHCI_LEGSUP_BIOS_OWNED) == 0u) {
                break;
            }
            usb_delay_us(10u);
        }
        if ((legsup & EHCI_LEGSUP_BIOS_OWNED) != 0u) {
            serial_write("[USB] EHCI BIOS handoff timeout\n");
        }
    }

    if (eecp <= 0xF8u) {
        pci_write32(dev->bus, dev->slot, dev->function, (uint8_t)(eecp + 4u), 0u);
    }
}

void usb_xhci_legacy_handoff(const pci_device_t* dev, uint64_t mmio_virt) {
    (void)dev;

    if (mmio_virt == 0u) {
        return;
    }

    uint32_t hccparams1 = usb_mmio_read32(mmio_virt, XHCI_CAP_HCCPARAMS1);
    uint16_t ext_off = (uint16_t)((hccparams1 >> 16) & 0xFFFFu);
    if (ext_off == 0u) {
        return;
    }

    uint32_t cur = (uint32_t)ext_off * 4u;
    for (uint32_t hops = 0u; hops < 64u && cur >= 0x40u; hops++) {
        uint32_t cap = usb_mmio_read32(mmio_virt, cur);
        uint8_t cap_id = (uint8_t)(cap & 0xFFu);
        uint8_t next = (uint8_t)((cap >> 8) & 0xFFu);

        if (cap_id == XHCI_EXT_CAP_ID_LEGACY) {
            uint32_t legsup = cap;
            if ((legsup & XHCI_LEGSUP_BIOS_OWNED) != 0u) {
                usb_mmio_write32(mmio_virt, cur, legsup | XHCI_LEGSUP_OS_OWNED);
                for (uint32_t i = 0u; i < USB_HANDOFF_POLL_LIMIT; i++) {
                    legsup = usb_mmio_read32(mmio_virt, cur);
                    if ((legsup & XHCI_LEGSUP_BIOS_OWNED) == 0u) {
                        break;
                    }
                    usb_delay_us(10u);
                }
                if ((legsup & XHCI_LEGSUP_BIOS_OWNED) != 0u) {
                    serial_write("[USB] xHCI BIOS handoff timeout\n");
                }
            }

            usb_mmio_write32(mmio_virt, (uint32_t)(cur + 4u), 0u);
            return;
        }

        if (next == 0u) {
            break;
        }
        cur += (uint32_t)next * 4u;
    }
}
