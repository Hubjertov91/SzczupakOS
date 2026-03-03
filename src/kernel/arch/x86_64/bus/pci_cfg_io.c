#include <drivers/pci.h>

#include <drivers/serial.h>
#include <firmware/acpi.h>
#include <kernel/io.h>
#include <kernel/mm/vmm.h>
#include <kernel/string.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_CAPABILITY_LIST_STATUS_BIT 0x0010u
#define PCI_CAP_ID_EXP 0x10u

typedef enum {
    PCI_CFG_BACKEND_LEGACY_IO = 0,
    PCI_CFG_BACKEND_ECAM = 1
} pci_cfg_backend_t;

typedef struct {
    bool valid;
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t bus_start;
    uint8_t bus_end;
} pci_ecam_region_t;

static pci_device_t g_pci_devices[PCI_MAX_DEVICES];
static uint16_t g_pci_device_count = 0;
static uint16_t g_pcie_device_count = 0;
static bool g_pci_scanned = false;
static pci_cfg_backend_t g_cfg_backend = PCI_CFG_BACKEND_LEGACY_IO;
static pci_ecam_region_t g_ecam;

static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return (1u << 31) |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           ((uint32_t)offset & 0xFCu);
}

static bool pci_is_valid_bdf(uint8_t slot, uint8_t function) {
    return slot < 32u && function < 8u;
}

static uint32_t pci_legacy_read32_raw(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

static void pci_legacy_write32_raw(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

static bool pci_ecam_address(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint64_t* out_phys) {
    if (!out_phys) return false;
    if (!g_ecam.valid) return false;
    if (!pci_is_valid_bdf(slot, function)) return false;
    if ((offset & 0x3u) != 0u) return false;
    if (offset > 0xFFCu) return false;
    if (bus < g_ecam.bus_start || bus > g_ecam.bus_end) return false;

    uint64_t bus_index = (uint64_t)(bus - g_ecam.bus_start);
    uint64_t phys = g_ecam.base_address +
                    (bus_index << 20) +
                    ((uint64_t)slot << 15) +
                    ((uint64_t)function << 12) +
                    (uint64_t)(offset & 0xFFCu);

    if (phys + 3u < phys || phys + 3u >= 0x100000000ULL) {
        return false;
    }
    *out_phys = phys;
    return true;
}

static uint32_t pci_cfg_read32_aligned(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset) {
    if (!pci_is_valid_bdf(slot, function)) return 0xFFFFFFFFu;
    if ((offset & 0x3u) != 0u || offset > 0xFFCu) return 0xFFFFFFFFu;

    if (g_cfg_backend == PCI_CFG_BACKEND_ECAM) {
        uint64_t ecam_phys = 0u;
        if (pci_ecam_address(bus, slot, function, offset, &ecam_phys)) {
            const volatile uint32_t* reg = (const volatile uint32_t*)PHYS_TO_VIRT(ecam_phys);
            return *reg;
        }
    }

    if (offset > 0xFFu) return 0xFFFFFFFFu;
    return pci_legacy_read32_raw(bus, slot, function, (uint8_t)offset);
}

static void pci_cfg_write32_aligned(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint32_t value) {
    if (!pci_is_valid_bdf(slot, function)) return;
    if ((offset & 0x3u) != 0u || offset > 0xFFCu) return;

    if (g_cfg_backend == PCI_CFG_BACKEND_ECAM) {
        uint64_t ecam_phys = 0u;
        if (pci_ecam_address(bus, slot, function, offset, &ecam_phys)) {
            volatile uint32_t* reg = (volatile uint32_t*)PHYS_TO_VIRT(ecam_phys);
            *reg = value;
            return;
        }
    }

    if (offset > 0xFFu) return;
    pci_legacy_write32_raw(bus, slot, function, (uint8_t)offset, value);
}

uint32_t pci_read32_ext(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset) {
    uint16_t aligned = (uint16_t)(offset & ~0x3u);
    return pci_cfg_read32_aligned(bus, slot, function, aligned);
}

uint16_t pci_read16_ext(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset) {
    uint32_t value = pci_read32_ext(bus, slot, function, offset);
    uint8_t shift = (uint8_t)((offset & 2u) * 8u);
    return (uint16_t)((value >> shift) & 0xFFFFu);
}

uint8_t pci_read8_ext(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset) {
    uint32_t value = pci_read32_ext(bus, slot, function, offset);
    uint8_t shift = (uint8_t)((offset & 3u) * 8u);
    return (uint8_t)((value >> shift) & 0xFFu);
}

void pci_write32_ext(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint32_t value) {
    uint16_t aligned = (uint16_t)(offset & ~0x3u);
    pci_cfg_write32_aligned(bus, slot, function, aligned, value);
}

void pci_write16_ext(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint16_t value) {
    uint16_t aligned = (uint16_t)(offset & ~0x3u);
    uint8_t shift = (uint8_t)((offset & 2u) * 8u);
    uint32_t reg = pci_cfg_read32_aligned(bus, slot, function, aligned);
    uint32_t mask = 0xFFFFu << shift;
    uint32_t next = (reg & ~mask) | ((uint32_t)value << shift);
    pci_cfg_write32_aligned(bus, slot, function, aligned, next);
}

void pci_write8_ext(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint8_t value) {
    uint16_t aligned = (uint16_t)(offset & ~0x3u);
    uint8_t shift = (uint8_t)((offset & 3u) * 8u);
    uint32_t reg = pci_cfg_read32_aligned(bus, slot, function, aligned);
    uint32_t mask = 0xFFu << shift;
    uint32_t next = (reg & ~mask) | ((uint32_t)value << shift);
    pci_cfg_write32_aligned(bus, slot, function, aligned, next);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return pci_read32_ext(bus, slot, function, offset);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    pci_write32_ext(bus, slot, function, offset, value);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return pci_read16_ext(bus, slot, function, offset);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return pci_read8_ext(bus, slot, function, offset);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    pci_write16_ext(bus, slot, function, offset, value);
}

void pci_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value) {
    pci_write8_ext(bus, slot, function, offset, value);
}

bool pci_set_command_bits(uint8_t bus, uint8_t slot, uint8_t function, uint16_t bits) {
    uint16_t command = pci_read16(bus, slot, function, 0x04u);
    uint16_t updated = (uint16_t)(command | bits);
    if (updated != command) {
        pci_write16(bus, slot, function, 0x04u, updated);
    }
    return true;
}

uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t function, uint8_t bar_index) {
    if (bar_index >= 6u) {
        return 0u;
    }
    return pci_read32(bus, slot, function, (uint8_t)(0x10u + (bar_index * 4u)));
}

bool pci_has_extended_config(void) {
    return g_cfg_backend == PCI_CFG_BACKEND_ECAM;
}

static bool pci_has_capabilities(uint8_t bus, uint8_t slot, uint8_t function) {
    uint16_t status = pci_read16(bus, slot, function, 0x06u);
    return (status & PCI_CAPABILITY_LIST_STATUS_BIT) != 0u;
}

static bool pci_detect_pcie_capability(uint8_t bus, uint8_t slot, uint8_t function) {
    if (!pci_has_capabilities(bus, slot, function)) {
        return false;
    }

    uint8_t cap_ptr = (uint8_t)(pci_read8(bus, slot, function, 0x34u) & 0xFCu);
    for (uint8_t hops = 0; hops < 48u && cap_ptr >= 0x40u; hops++) {
        uint8_t cap_id = pci_read8(bus, slot, function, cap_ptr);
        if (cap_id == PCI_CAP_ID_EXP) {
            return true;
        }

        uint8_t next_ptr = (uint8_t)(pci_read8(bus, slot, function, (uint8_t)(cap_ptr + 1u)) & 0xFCu);
        if (next_ptr == cap_ptr) {
            break;
        }
        cap_ptr = next_ptr;
    }

    return false;
}

static void pci_config_select_backend(void) {
    g_cfg_backend = PCI_CFG_BACKEND_LEGACY_IO;
    memset(&g_ecam, 0, sizeof(g_ecam));

    acpi_mcfg_info_t mcfg;
    if (!acpi_get_mcfg_info(&mcfg) || !mcfg.present) {
        serial_write("[PCI] Config backend: legacy I/O (no MCFG)\n");
        return;
    }

    if (mcfg.segment_group != 0u) {
        serial_write("[PCI] MCFG segment != 0 not supported yet, using legacy I/O\n");
        return;
    }
    if (mcfg.bus_end < mcfg.bus_start || mcfg.base_address == 0u) {
        serial_write("[PCI] Invalid MCFG segment, using legacy I/O\n");
        return;
    }

    uint64_t bus_count = (uint64_t)(mcfg.bus_end - mcfg.bus_start) + 1u;
    uint64_t region_last = mcfg.base_address + (bus_count << 20) - 1u;
    if (region_last < mcfg.base_address || region_last >= 0x100000000ULL) {
        serial_write("[PCI] MCFG region outside mapped physical range, using legacy I/O\n");
        return;
    }

    g_ecam.valid = true;
    g_ecam.base_address = mcfg.base_address;
    g_ecam.segment_group = mcfg.segment_group;
    g_ecam.bus_start = mcfg.bus_start;
    g_ecam.bus_end = mcfg.bus_end;
    g_cfg_backend = PCI_CFG_BACKEND_ECAM;

    serial_write("[PCIe] Config backend: ECAM base=");
    serial_write_hex(g_ecam.base_address);
    serial_write(" seg=");
    serial_write_dec(g_ecam.segment_group);
    serial_write(" bus=");
    serial_write_dec(g_ecam.bus_start);
    serial_write("-");
    serial_write_dec(g_ecam.bus_end);
    serial_write("\n");
}

static void pci_enumerate(void) {
    g_pci_device_count = 0;
    g_pcie_device_count = 0;
    memset(g_pci_devices, 0, sizeof(g_pci_devices));

    for (uint16_t bus = 0; bus < 256u; bus++) {
        for (uint8_t slot = 0; slot < 32u; slot++) {
            uint16_t vendor0 = pci_read16((uint8_t)bus, slot, 0u, 0x00u);
            if (vendor0 == 0xFFFFu) {
                continue;
            }

            uint8_t header_type0 = pci_read8((uint8_t)bus, slot, 0u, 0x0Eu);
            uint8_t function_count = (header_type0 & 0x80u) ? 8u : 1u;

            for (uint8_t function = 0; function < function_count; function++) {
                uint16_t vendor = pci_read16((uint8_t)bus, slot, function, 0x00u);
                if (vendor == 0xFFFFu) {
                    continue;
                }

                if (g_pci_device_count >= PCI_MAX_DEVICES) {
                    continue;
                }

                uint32_t id = pci_read32((uint8_t)bus, slot, function, 0x00u);
                uint32_t class_info = pci_read32((uint8_t)bus, slot, function, 0x08u);
                uint8_t header_type = (uint8_t)(pci_read8((uint8_t)bus, slot, function, 0x0Eu) & 0x7Fu);
                bool is_pcie = pci_detect_pcie_capability((uint8_t)bus, slot, function);

                pci_device_t* out = &g_pci_devices[g_pci_device_count++];
                out->bus = (uint8_t)bus;
                out->slot = slot;
                out->function = function;
                out->vendor_id = (uint16_t)(id & 0xFFFFu);
                out->device_id = (uint16_t)((id >> 16) & 0xFFFFu);
                out->revision_id = (uint8_t)(class_info & 0xFFu);
                out->prog_if = (uint8_t)((class_info >> 8) & 0xFFu);
                out->subclass = (uint8_t)((class_info >> 16) & 0xFFu);
                out->class_code = (uint8_t)((class_info >> 24) & 0xFFu);
                out->header_type = header_type;
                out->is_pcie = is_pcie;

                if (is_pcie) {
                    g_pcie_device_count++;
                }
            }
        }
    }
}

bool pci_init(void) {
    if (g_pci_scanned) {
        return true;
    }

    pci_config_select_backend();
    pci_enumerate();
    g_pci_scanned = true;

    serial_write("[PCIe] Enumerated ");
    serial_write_dec(g_pci_device_count);
    serial_write(" PCI devices, PCIe capability on ");
    serial_write_dec(g_pcie_device_count);
    serial_write("\n");

    return true;
}

uint16_t pci_get_device_count(void) {
    if (!g_pci_scanned) {
        pci_init();
    }
    return g_pci_device_count;
}

bool pci_get_device(uint16_t index, pci_device_t* out_device) {
    if (!out_device) {
        return false;
    }
    if (!g_pci_scanned) {
        pci_init();
    }
    if (index >= g_pci_device_count) {
        return false;
    }

    *out_device = g_pci_devices[index];
    return true;
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out_device) {
    if (!g_pci_scanned) {
        pci_init();
    }

    for (uint16_t i = 0; i < g_pci_device_count; i++) {
        if (g_pci_devices[i].vendor_id == vendor_id &&
            g_pci_devices[i].device_id == device_id) {
            if (out_device) {
                *out_device = g_pci_devices[i];
            }
            return true;
        }
    }

    return false;
}
