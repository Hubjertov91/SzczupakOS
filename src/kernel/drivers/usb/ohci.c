#include "usb_priv.h"

bool usb_probe_ohci(const pci_device_t* dev, usb_controller_info_t* info) {
    if (!dev || !info || info->mmio_base == 0u) {
        return false;
    }
    if (info->mmio_base >= USB_MMIO_LIMIT_4G) {
        serial_write("[USB] OHCI MMIO above 4GiB is not mapped in this build\n");
        return false;
    }

    (void)pci_set_command_bits(dev->bus, dev->slot, dev->function,
                               PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

    uint64_t mmio_virt = (uint64_t)PHYS_TO_VIRT(info->mmio_base);

    usb_mmio_write32(mmio_virt, OHCI_REG_CMDSTATUS, OHCI_CMDSTATUS_HCR);
    if (!usb_wait_mmio32_clear(mmio_virt, OHCI_REG_CMDSTATUS, OHCI_CMDSTATUS_HCR, USB_PROBE_POLL_LIMIT)) {
        serial_write("[USB] OHCI reset timeout\n");
        return false;
    }

    uint32_t hc_control = usb_mmio_read32(mmio_virt, OHCI_REG_CONTROL);
    hc_control &= ~OHCI_CONTROL_HCFS_MASK;
    hc_control |= OHCI_CONTROL_HCFS_OPERATIONAL;
    usb_mmio_write32(mmio_virt, OHCI_REG_CONTROL, hc_control);

    uint8_t max_ports = (uint8_t)(usb_mmio_read32(mmio_virt, OHCI_REG_RHDESCA) & 0xFFu);
    uint8_t connected = 0u;
    for (uint8_t port = 1u; port <= max_ports; port++) {
        uint32_t portsc = usb_mmio_read32(mmio_virt, OHCI_REG_RHPORT_BASE + ((uint32_t)(port - 1u) * OHCI_PORT_STRIDE));
        if ((portsc & OHCI_RHPORT_CCS) == 0u) continue;

        connected++;
        serial_write("[USB] OHCI port ");
        serial_write_dec((uint32_t)port);
        serial_write(": connected, speed=");
        serial_write(usb_full_or_low_speed_name((portsc & OHCI_RHPORT_LSDA) != 0u));
        serial_write("\n");
    }

    info->port_count = max_ports;
    info->connected_ports = connected;
    return true;
}
