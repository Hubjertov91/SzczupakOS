#include "usb_priv.h"

bool usb_init(void) {
    if (g_usb_scanned) {
        return g_usb_count > 0u;
    }

    g_usb_scanned = true;
    g_usb_count = 0u;
    g_next_usb_address = 1u;
    memset(g_usb_controllers, 0, sizeof(g_usb_controllers));
    memset(g_uhci_hosts, 0, sizeof(g_uhci_hosts));
    memset(g_hid_devices, 0, sizeof(g_hid_devices));

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

        uint8_t ctrl_index = (uint8_t)g_usb_count;
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

        if (out->controller_type == USB_CTRL_UHCI) {
            out->initialized = usb_probe_uhci(&dev, out, ctrl_index);
        } else if (out->controller_type == USB_CTRL_OHCI) {
            out->initialized = usb_probe_ohci(&dev, out);
        } else if (out->controller_type == USB_CTRL_EHCI) {
            out->initialized = usb_probe_ehci(&dev, out);
        } else if (out->controller_type == USB_CTRL_XHCI) {
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
