#include <drivers/usb.h>

#include <drivers/pci.h>
#include <drivers/serial.h>
#include <mm/vmm.h>
#include <kernel/string.h>

#define PCI_CLASS_SERIAL_BUS 0x0Cu
#define PCI_SUBCLASS_USB     0x03u
#define USB_MMIO_LIMIT_4G    0x100000000ULL

#define XHCI_CAP_CAPLENGTH   0x00u
#define XHCI_CAP_HCSPARAMS1  0x04u

#define XHCI_OP_USBSTS       0x04u
#define XHCI_OP_PORTSC_BASE  0x400u
#define XHCI_OP_PORTSC_STRIDE 0x10u

#define XHCI_USBSTS_CNR      (1u << 11)
#define XHCI_PORTSC_CCS      (1u << 0)
#define XHCI_PORTSC_SPEED_SHIFT 10u
#define XHCI_PORTSC_SPEED_MASK  (0xFu << XHCI_PORTSC_SPEED_SHIFT)

static usb_controller_info_t g_usb_controllers[USB_MAX_CONTROLLERS];
static uint16_t g_usb_count = 0;
static bool g_usb_scanned = false;

static uint8_t usb_mmio_read8(uint64_t base, uint32_t offset) {
    volatile uint8_t* reg = (volatile uint8_t*)(base + (uint64_t)offset);
    return *reg;
}

static uint32_t usb_mmio_read32(uint64_t base, uint32_t offset) {
    volatile uint32_t* reg = (volatile uint32_t*)(base + (uint64_t)offset);
    return *reg;
}

static uint8_t usb_controller_type_from_prog_if(uint8_t prog_if) {
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

static const char* usb_xhci_speed_name(uint8_t speed_code) {
    switch (speed_code) {
        case 1u: return "Full";
        case 2u: return "Low";
        case 3u: return "High";
        case 4u: return "Super";
        case 5u: return "Super+";
        default: return "Unknown";
    }
}

static void usb_parse_resources(uint8_t bus, uint8_t slot, uint8_t function,
                                uint32_t* out_io_base, uint64_t* out_mmio_base) {
    if (!out_io_base || !out_mmio_base) {
        return;
    }

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

static bool usb_probe_xhci(const pci_device_t* dev, usb_controller_info_t* info) {
    if (!dev || !info || info->mmio_base == 0u) {
        return false;
    }

    if (info->mmio_base >= USB_MMIO_LIMIT_4G) {
        serial_write("[USB] xHCI MMIO above 4GiB is not mapped in this build\n");
        return false;
    }

    (void)pci_set_command_bits(dev->bus, dev->slot, dev->function, PCI_COMMAND_MEMORY_SPACE);

    uint64_t mmio_virt = (uint64_t)PHYS_TO_VIRT(info->mmio_base);
    uint8_t cap_length = usb_mmio_read8(mmio_virt, XHCI_CAP_CAPLENGTH);
    if (cap_length < 0x20u) {
        serial_write("[USB] xHCI invalid CAPLENGTH\n");
        return false;
    }

    uint32_t hcsparams1 = usb_mmio_read32(mmio_virt, XHCI_CAP_HCSPARAMS1);
    uint8_t max_ports = (uint8_t)((hcsparams1 >> 24) & 0xFFu);
    uint64_t op_base = mmio_virt + (uint64_t)cap_length;

    uint32_t usbsts = usb_mmio_read32(op_base, XHCI_OP_USBSTS);
    if ((usbsts & XHCI_USBSTS_CNR) != 0u) {
        serial_write("[USB] xHCI controller not ready (CNR)\n");
    }

    uint8_t connected = 0u;
    for (uint8_t port = 1u; port <= max_ports; port++) {
        uint32_t port_offset = XHCI_OP_PORTSC_BASE + ((uint32_t)(port - 1u) * XHCI_OP_PORTSC_STRIDE);
        uint32_t portsc = usb_mmio_read32(op_base, port_offset);
        if ((portsc & XHCI_PORTSC_CCS) == 0u) {
            continue;
        }

        connected++;
        uint8_t speed_code = (uint8_t)((portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT);

        serial_write("[USB] xHCI port ");
        serial_write_dec((uint32_t)port);
        serial_write(": connected, speed=");
        serial_write(usb_xhci_speed_name(speed_code));
        serial_write("\n");
    }

    info->port_count = max_ports;
    info->connected_ports = connected;
    return true;
}

static void usb_log_controller(const usb_controller_info_t* info) {
    if (!info) {
        return;
    }

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

bool usb_init(void) {
    if (g_usb_scanned) {
        return g_usb_count > 0u;
    }

    g_usb_scanned = true;
    g_usb_count = 0u;
    memset(g_usb_controllers, 0, sizeof(g_usb_controllers));

    uint16_t pci_count = pci_get_device_count();
    for (uint16_t i = 0u; i < pci_count; i++) {
        pci_device_t dev;
        if (!pci_get_device(i, &dev)) {
            continue;
        }

        if (dev.class_code != PCI_CLASS_SERIAL_BUS || dev.subclass != PCI_SUBCLASS_USB) {
            continue;
        }

        if (g_usb_count >= USB_MAX_CONTROLLERS) {
            break;
        }

        usb_controller_info_t* out = &g_usb_controllers[g_usb_count++];
        memset(out, 0, sizeof(*out));
        out->bus = dev.bus;
        out->slot = dev.slot;
        out->function = dev.function;
        out->controller_type = usb_controller_type_from_prog_if(dev.prog_if);
        out->class_code = dev.class_code;
        out->subclass = dev.subclass;
        out->prog_if = dev.prog_if;
        out->vendor_id = dev.vendor_id;
        out->device_id = dev.device_id;
        out->is_pcie = dev.is_pcie;

        usb_parse_resources(dev.bus, dev.slot, dev.function, &out->io_base, &out->mmio_base);
        out->initialized = false;
        out->port_count = 0u;
        out->connected_ports = 0u;
        if (out->controller_type == USB_CTRL_XHCI) {
            out->initialized = usb_probe_xhci(&dev, out);
        }
        usb_log_controller(out);
    }

    if (g_usb_count == 0u) {
        serial_write("[USB] No host controllers detected\n");
        return false;
    }

    serial_write("[USB] Controllers detected: ");
    serial_write_dec(g_usb_count);
    serial_write("\n");
    return true;
}

uint16_t usb_get_controller_count(void) {
    if (!g_usb_scanned) {
        usb_init();
    }
    return g_usb_count;
}

bool usb_get_controller(uint16_t index, usb_controller_info_t* out_info) {
    if (!out_info) {
        return false;
    }
    if (!g_usb_scanned) {
        usb_init();
    }
    if (index >= g_usb_count) {
        return false;
    }

    *out_info = g_usb_controllers[index];
    return true;
}
