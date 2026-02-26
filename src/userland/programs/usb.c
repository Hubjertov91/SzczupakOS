#include <stdio.h>
#include <stdint.h>
#include <syscall.h>

static char hex_digit(uint8_t v) {
    return (v < 10u) ? (char)('0' + v) : (char)('A' + (v - 10u));
}

static void print_hex8(uint8_t v) {
    putchar(hex_digit((uint8_t)((v >> 4) & 0x0Fu)));
    putchar(hex_digit((uint8_t)(v & 0x0Fu)));
}

static void print_hex16(uint16_t v) {
    print_hex8((uint8_t)((v >> 8) & 0xFFu));
    print_hex8((uint8_t)(v & 0xFFu));
}

static void print_hex32(uint32_t v) {
    print_hex16((uint16_t)((v >> 16) & 0xFFFFu));
    print_hex16((uint16_t)(v & 0xFFFFu));
}

static void print_hex64(uint64_t v) {
    print_hex32((uint32_t)((v >> 32) & 0xFFFFFFFFu));
    print_hex32((uint32_t)(v & 0xFFFFFFFFu));
}

static const char* usb_type_name(uint8_t type) {
    switch (type) {
        case 1u: return "UHCI";
        case 2u: return "OHCI";
        case 3u: return "EHCI";
        case 4u: return "xHCI";
        default: return "USB";
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    long count = sys_usb_get_count();
    if (count < 0) {
        printf("usb: unavailable\n");
        sys_exit(1);
        return 1;
    }

    printf("USB host controllers: %ld\n", count);
    if (count == 0) {
        sys_exit(0);
        return 0;
    }

    for (uint16_t i = 0; i < (uint16_t)count; i++) {
        struct usb_controller_info ctrl;
        if (sys_usb_get_controller(i, &ctrl) < 0) {
            continue;
        }

        print_hex8(ctrl.bus);
        putchar(':');
        print_hex8(ctrl.slot);
        putchar('.');
        print_hex8(ctrl.function);
        printf("  %s  ", usb_type_name(ctrl.controller_type));
        print_hex16(ctrl.vendor_id);
        putchar(':');
        print_hex16(ctrl.device_id);
        printf("  %s  %s",
               ctrl.is_pcie ? "PCIe" : "PCI",
               ctrl.initialized ? "probed" : "detected");
        if (ctrl.port_count != 0u) {
            printf("  ports=%u connected=%u",
                   (unsigned)ctrl.port_count,
                   (unsigned)ctrl.connected_ports);
        }
        if (ctrl.io_base != 0u) {
            printf("  io=0x");
            print_hex32(ctrl.io_base);
        }
        if (ctrl.mmio_base != 0u) {
            printf("  mmio=0x");
            print_hex64(ctrl.mmio_base);
        }
        putchar('\n');
    }

    sys_exit(0);
    return 0;
}
