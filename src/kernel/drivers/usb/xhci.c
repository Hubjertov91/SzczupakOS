#include "usb_priv.h"

bool usb_probe_xhci(const pci_device_t* dev, usb_controller_info_t* info) {
    if (!dev || !info || info->mmio_base == 0u) {
        return false;
    }

    if (info->mmio_base >= USB_MMIO_LIMIT_4G) {
        serial_write("[USB] xHCI MMIO above 4GiB is not mapped in this build\n");
        return false;
    }

    (void)pci_set_command_bits(dev->bus, dev->slot, dev->function,
                               PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

    uint64_t mmio_virt = (uint64_t)PHYS_TO_VIRT(info->mmio_base);
    usb_xhci_legacy_handoff(dev, mmio_virt);

    uint8_t cap_length = usb_mmio_read8(mmio_virt, XHCI_CAP_CAPLENGTH);
    if (cap_length < 0x20u) {
        serial_write("[USB] xHCI invalid CAPLENGTH\n");
        return false;
    }

    uint32_t hcsparams1 = usb_mmio_read32(mmio_virt, XHCI_CAP_HCSPARAMS1);
    uint8_t max_ports = (uint8_t)((hcsparams1 >> 24) & 0xFFu);
    uint64_t op_base = mmio_virt + (uint64_t)cap_length;

    if (!usb_wait_mmio32_clear(op_base, XHCI_OP_USBSTS, XHCI_USBSTS_CNR, USB_PROBE_POLL_LIMIT * 4u)) {
        serial_write("[USB] xHCI controller not ready (CNR timeout)\n");
        return false;
    }
    usb_delay_ms(1u);

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
