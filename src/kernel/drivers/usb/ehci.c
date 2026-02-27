#include "usb_priv.h"

bool usb_probe_ehci(const pci_device_t* dev, usb_controller_info_t* info) {
    if (!dev || !info || info->mmio_base == 0u) {
        return false;
    }
    if (info->mmio_base >= USB_MMIO_LIMIT_4G) {
        serial_write("[USB] EHCI MMIO above 4GiB is not mapped in this build\n");
        return false;
    }

    (void)pci_set_command_bits(dev->bus, dev->slot, dev->function,
                               PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

    uint64_t mmio_virt = (uint64_t)PHYS_TO_VIRT(info->mmio_base);
    usb_ehci_legacy_handoff(dev, mmio_virt);

    uint8_t cap_length = usb_mmio_read8(mmio_virt, EHCI_CAP_CAPLENGTH);
    if (cap_length < 0x10u) {
        serial_write("[USB] EHCI invalid CAPLENGTH\n");
        return false;
    }

    uint32_t hcsparams = usb_mmio_read32(mmio_virt, EHCI_CAP_HCSPARAMS);
    uint8_t max_ports = (uint8_t)(hcsparams & 0x0Fu);
    uint64_t op_base = mmio_virt + (uint64_t)cap_length;

    uint32_t usbcmd = usb_mmio_read32(op_base, EHCI_OP_USBCMD);
    usbcmd &= ~EHCI_USBCMD_RUN;
    usb_mmio_write32(op_base, EHCI_OP_USBCMD, usbcmd);
    (void)usb_wait_mmio32_set(op_base, EHCI_OP_USBSTS, EHCI_USBSTS_HCHALTED, USB_PROBE_POLL_LIMIT);

    usb_mmio_write32(op_base, EHCI_OP_USBCMD, usbcmd | EHCI_USBCMD_HCRESET);
    if (!usb_wait_mmio32_clear(op_base, EHCI_OP_USBCMD, EHCI_USBCMD_HCRESET, USB_PROBE_POLL_LIMIT)) {
        serial_write("[USB] EHCI reset timeout\n");
        return false;
    }
    usb_delay_ms(2u);

    usb_mmio_write32(op_base, EHCI_OP_USBSTS, 0x3Fu);
    usb_mmio_write32(op_base, EHCI_OP_USBINTR, 0u);
    usb_mmio_write32(op_base, EHCI_OP_CONFIGFLAG, 1u);
    usb_mmio_write32(op_base, EHCI_OP_USBCMD, EHCI_USBCMD_RUN);
    if (!usb_wait_mmio32_clear(op_base, EHCI_OP_USBSTS, EHCI_USBSTS_HCHALTED, USB_PROBE_POLL_LIMIT)) {
        serial_write("[USB] EHCI failed to leave halted state\n");
    }
    usb_delay_ms(10u);

    uint8_t connected = 0u;
    for (uint8_t port = 1u; port <= max_ports; port++) {
        uint32_t portsc = usb_mmio_read32(op_base,
                                          EHCI_OP_PORTSC_BASE + ((uint32_t)(port - 1u) * EHCI_OP_PORTSC_STRIDE));
        if ((portsc & EHCI_PORTSC_CCS) == 0u) continue;

        connected++;
        serial_write("[USB] EHCI port ");
        serial_write_dec((uint32_t)port);
        serial_write(": connected");
        if ((portsc & EHCI_PORTSC_OWNER) != 0u) {
            serial_write(" (owned by companion)");
        } else {
            serial_write(", speed=High");
        }
        serial_write("\n");
    }

    info->port_count = max_ports;
    info->connected_ports = connected;
    return true;
}
