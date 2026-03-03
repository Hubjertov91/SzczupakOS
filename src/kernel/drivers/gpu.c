#include <drivers/gpu.h>

#include <drivers/pci.h>
#include <drivers/serial.h>

#define PCI_CLASS_DISPLAY        0x03u
#define PCI_SUBCLASS_VGA         0x00u
#define PCI_SUBCLASS_3D          0x02u

typedef struct {
    gpu_info_t info;
    bool initialized;
} gpu_state_t;

static gpu_state_t g_gpu;

static bool gpu_is_display(const pci_device_t* dev) {
    return dev && dev->class_code == PCI_CLASS_DISPLAY;
}

static bool gpu_is_better_candidate(const pci_device_t* current, const pci_device_t* best) {
    if (!current) return false;
    if (!best) return true;

    /* Prefer VGA-compatible devices, then 3D controllers, then any display. */
    if (current->subclass == PCI_SUBCLASS_VGA && best->subclass != PCI_SUBCLASS_VGA) return true;
    if (current->subclass != PCI_SUBCLASS_VGA && best->subclass == PCI_SUBCLASS_VGA) return false;

    if (current->subclass == PCI_SUBCLASS_3D && best->subclass != PCI_SUBCLASS_3D) return true;
    if (current->subclass != PCI_SUBCLASS_3D && best->subclass == PCI_SUBCLASS_3D) return false;

    return false;
}

static void gpu_select_bar_bases(gpu_info_t* out, const pci_device_t* dev) {
    if (!out || !dev) return;

    out->mmio_base = 0u;
    out->io_base = 0u;

    for (uint8_t bar = 0; bar < 6u; bar++) {
        uint32_t raw = pci_read_bar(dev->bus, dev->slot, dev->function, bar);
        if (raw == 0u || raw == 0xFFFFFFFFu) continue;

        if (raw & 0x1u) {
            uint64_t io_base = (uint64_t)(raw & ~0x3u);
            if (io_base != 0u && out->io_base == 0u) {
                out->io_base = io_base;
            }
            continue;
        }

        uint8_t type = (uint8_t)((raw >> 1) & 0x3u);
        uint64_t mmio_base = (uint64_t)(raw & ~0xFu);
        if (type == 0x2u && bar < 5u) {
            uint32_t high = pci_read_bar(dev->bus, dev->slot, dev->function, (uint8_t)(bar + 1u));
            mmio_base |= ((uint64_t)high << 32);
            bar++;
        }

        if (mmio_base != 0u && out->mmio_base == 0u) {
            out->mmio_base = mmio_base;
        }
    }
}

static void gpu_log_detected(const gpu_info_t* info) {
    if (!info || !info->present) return;

    serial_write("[GPU] Detected display controller at ");
    serial_write_hex(((uint64_t)info->bus << 16) |
                     ((uint64_t)info->slot << 8) |
                     (uint64_t)info->function);
    serial_write(" VID:DID=");
    serial_write_hex(((uint64_t)info->vendor_id << 16) | (uint64_t)info->device_id);
    serial_write(" class=");
    serial_write_hex(((uint64_t)info->class_code << 8) | (uint64_t)info->subclass);
    serial_write(" prog_if=");
    serial_write_hex(info->prog_if);
    serial_write("\n");

    if (info->mmio_base) {
        serial_write("[GPU] MMIO BAR=");
        serial_write_hex(info->mmio_base);
        serial_write("\n");
    }
    if (info->io_base) {
        serial_write("[GPU] IO BAR=");
        serial_write_hex(info->io_base);
        serial_write("\n");
    }
}

bool gpu_init(void) {
    g_gpu.initialized = true;
    g_gpu.info.present = false;
    g_gpu.info.mmio_base = 0u;
    g_gpu.info.io_base = 0u;

    uint16_t count = pci_get_device_count();
    bool found = false;
    pci_device_t best = {0};

    for (uint16_t i = 0; i < count; i++) {
        pci_device_t dev;
        if (!pci_get_device(i, &dev)) continue;
        if (!gpu_is_display(&dev)) continue;

        if (!found || gpu_is_better_candidate(&dev, &best)) {
            best = dev;
            found = true;
        }
    }

    if (!found) {
        serial_write("[GPU] No PCI display controller detected\n");
        return false;
    }

    g_gpu.info.present = true;
    g_gpu.info.bus = best.bus;
    g_gpu.info.slot = best.slot;
    g_gpu.info.function = best.function;
    g_gpu.info.class_code = best.class_code;
    g_gpu.info.subclass = best.subclass;
    g_gpu.info.prog_if = best.prog_if;
    g_gpu.info.vendor_id = best.vendor_id;
    g_gpu.info.device_id = best.device_id;
    gpu_select_bar_bases(&g_gpu.info, &best);

    uint16_t cmd_bits = PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    if (g_gpu.info.io_base != 0u) {
        cmd_bits |= PCI_COMMAND_IO_SPACE;
    }
    (void)pci_set_command_bits(best.bus, best.slot, best.function, cmd_bits);

    gpu_log_detected(&g_gpu.info);
    serial_write("[GPU] Backend selected: pci-display\n");
    return true;
}

bool gpu_available(void) {
    return g_gpu.initialized && g_gpu.info.present;
}

bool gpu_get_info(gpu_info_t* out_info) {
    if (!out_info) return false;
    if (!gpu_available()) return false;
    *out_info = g_gpu.info;
    return true;
}

const char* gpu_backend_name(void) {
    return gpu_available() ? "pci-display" : "none";
}
