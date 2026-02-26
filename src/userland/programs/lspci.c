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

static const char* pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Simple comms";
        case 0x08: return "Base system";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal processing";
        case 0x12: return "Processing accelerator";
        case 0x13: return "Non-essential instrumentation";
        case 0x40: return "Co-processor";
        default: return "Unknown";
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    long count = sys_pci_get_count();
    if (count < 0) {
        printf("lspci: unavailable\n");
        sys_exit(1);
        return 1;
    }

    printf("PCI devices: %ld\n", count);
    if (count == 0) {
        sys_exit(0);
        return 0;
    }

    for (uint16_t i = 0; i < (uint16_t)count; i++) {
        struct pci_device_info dev;
        if (sys_pci_get_device(i, &dev) < 0) {
            continue;
        }

        print_hex8(dev.bus);
        putchar(':');
        print_hex8(dev.slot);
        putchar('.');
        print_hex8(dev.function);
        printf("  ");
        print_hex16(dev.vendor_id);
        putchar(':');
        print_hex16(dev.device_id);
        printf("  class ");
        print_hex8(dev.class_code);
        putchar('/');
        print_hex8(dev.subclass);
        putchar('/');
        print_hex8(dev.prog_if);
        printf("  %s  %s\n", dev.is_pcie ? "PCIe" : "PCI", pci_class_name(dev.class_code));
    }

    sys_exit(0);
    return 0;
}
